--
-- Swipe
--
-- Reusable swipe detection for any Lua layer. It is the drag handling of the start
-- menu (MenuButton::ccTouchMoved / ccTouchEnded in StartMenu.cpp) made into a
-- module, with three additions:
--
--   * the axis is a choice: 'vertical' (up / down), 'horizontal' (left / right) or
--     'both'
--   * "anywhere" mode: a swipe may start on empty background. The original only
--     counted a swipe that began on a button.
--   * it never swallows the touch by default, so buttons and menus under it keep
--     working exactly as before
--
-- Usage:
--
--     local handle = swipe.attach(self, {          -- self: any layer or scene node
--         direction = 'both',                      -- 'vertical' | 'horizontal' | 'both'
--         anywhere  = true,                        -- swipes may start on empty background
--         onSwipe   = function(dir, info)          -- dir: 'up' 'down' 'left' 'right'
--             log('swiped ' .. dir)
--         end,
--         onTap     = function(x, y) end,          -- touch ended without a swipe
--     })
--     handle:setEnabled(false)   -- pause it
--     handle:detach()            -- remove it
--
-- Options (all optional):
--
--     direction  'vertical' (default), 'horizontal' or 'both'. With 'both' the axis the
--                finger moved further along wins.
--     threshold  how far the finger must travel to count, in design units (default 16,
--                the start menu's value). It has to be more than that, as in the menu.
--     anywhere   true: a swipe may start anywhere on the screen (startArea is ignored).
--     startArea  where a swipe may start when anywhere is not true: a node (its
--                bounding box), a CCRect, a table {x=, y=, width=, height=} or a
--                function(x, y) returning true. Without one a swipe may start anywhere.
--     fireOn     'ended' (default, like the menu: the swipe counts when the finger is
--                lifted) or 'moved' (as soon as the finger has travelled far enough,
--                once per touch).
--     onSwipe    function(direction, info)
--     on         { up = fn, down = fn, left = fn, right = fn } (fn(info)), as an
--                alternative to one onSwipe
--     onTap      function(x, y)   touch ended without a swipe
--     onBegan    function(x, y)
--     onDrag     function(dx, dy, info)   every move, distance from where it began
--     onCancel   function()
--     swallow    true: nothing under this layer gets the touch it tracks (default false)
--     priority   touch priority, lower is first (default -1000: it sees every touch
--                before menus and buttons do, and because it does not swallow, they
--                still get it)
--     zOrder     z order of the helper layer (default 10000)
--
-- info = { direction, dx, dy, startX, startY, endX, endY }  (dy > 0 is up)
--
-- Difference from the start menu: the distance is measured from where the finger
-- touched down, the menu measured it from the first move it saw. And a swipe is
-- decided by where the finger is when it is lifted, so a finger that comes back
-- to where it started is a tap, not a swipe.
--
swipe = {}

swipe.defaults = {
    direction = 'vertical',
    threshold = 16,
    fireOn = 'ended',
    priority = -1000,
    zOrder = 10000
}

local AXES = {vertical = true, horizontal = true, both = true}
local FIRE_ON = {ended = true, moved = true}

local function absolute(v) return v < 0 and -v or v end

-- A callback that fails must not break the touch handling around it.
local function call(fn, ...)
    if not fn then return end
    local ok, err = pcall(fn, ...)
    if not ok then
        if log then
            log('swipe: callback error: ' .. tostring(err))
        else
            print('swipe: callback error: ' .. tostring(err))
        end
    end
end

---------------------------------------------------------------------------
-- Where may a swipe start
---------------------------------------------------------------------------

-- Bounding box of a node on screen (its parent's coordinates converted to world).
local function nodeWorldRect(node)
    local box = node:boundingBox()
    local x1, y1 = box.origin.x, box.origin.y
    local x2, y2 = x1 + box.size.width, y1 + box.size.height

    local parent = node:getParent()
    if parent then
        local a = parent:convertToWorldSpace(CCPoint(x1, y1))
        local b = parent:convertToWorldSpace(CCPoint(x2, y2))
        x1, y1, x2, y2 = a.x, a.y, b.x, b.y
    end

    return math.min(x1, x2), math.min(y1, y2), math.max(x1, x2), math.max(y1, y2)
end

local function rectTester(x, y, w, h)
    return function(px, py)
        return px >= x and px <= x + w and py >= y and py <= y + h
    end
end

-- Turns every accepted form of startArea into function(x, y) -> boolean.
local function makeAreaTester(area)
    if area == nil then return nil end

    if type(area) == 'function' then return area end

    if type(area) == 'table' then
        if area.origin and area.size then -- a CCRect-like table
            return rectTester(area.origin.x, area.origin.y, area.size.width, area.size.height)
        end
        assert(area.x and area.y and area.width and area.height,
               'swipe: startArea table needs x, y, width and height')
        return rectTester(area.x, area.y, area.width, area.height)
    end

    if type(area) == 'userdata' then
        local kind = tolua and tolua.type and tolua.type(area)
        if kind == 'CCRect' then
            return rectTester(area.origin.x, area.origin.y, area.size.width, area.size.height)
        end

        -- A node: looked up at every touch, since it can move or be resized.
        return function(px, py)
            local x1, y1, x2, y2 = nodeWorldRect(area)
            return px >= x1 and px <= x2 and py >= y1 and py <= y2
        end
    end

    error('swipe: unsupported startArea')
end

---------------------------------------------------------------------------
-- The recognizer: no engine in here, only numbers and callbacks
---------------------------------------------------------------------------

local Recognizer = {}
Recognizer.__index = Recognizer

function swipe.newRecognizer(options)
    options = options or {}

    local axis = options.direction or swipe.defaults.direction
    assert(AXES[axis], "swipe: direction must be 'vertical', 'horizontal' or 'both'")

    local fireOn = options.fireOn or swipe.defaults.fireOn
    assert(FIRE_ON[fireOn], "swipe: fireOn must be 'ended' or 'moved'")

    local self = setmetatable({}, Recognizer)
    self.axis = axis
    self.fireOn = fireOn
    self.threshold = options.threshold or swipe.defaults.threshold
    self.anywhere = options.anywhere == true
    self.areaTester = makeAreaTester(options.startArea)
    self.onSwipe = options.onSwipe
    self.byDirection = options.on
    self.onTap = options.onTap
    self.onBegan = options.onBegan
    self.onDrag = options.onDrag
    self.onCancel = options.onCancel
    self.enabled = true
    self.tracking = false
    self.fired = false
    self.startX, self.startY = 0, 0
    return self
end

-- Which way did the finger go, or nil if it has not gone far enough.
function Recognizer:resolve(dx, dy)
    local t = self.threshold
    local ax, ay = absolute(dx), absolute(dy)

    if self.axis == 'vertical' then
        if ay > t then return dy > 0 and 'up' or 'down' end
    elseif self.axis == 'horizontal' then
        if ax > t then return dx > 0 and 'right' or 'left' end
    else
        if ax > t or ay > t then
            if ay >= ax then return dy > 0 and 'up' or 'down' end
            return dx > 0 and 'right' or 'left'
        end
    end
    return nil
end

function Recognizer:makeInfo(direction, x, y)
    return {
        direction = direction,
        dx = x - self.startX,
        dy = y - self.startY,
        startX = self.startX,
        startY = self.startY,
        endX = x,
        endY = y
    }
end

function Recognizer:emit(direction, x, y)
    local info = self:makeInfo(direction, x, y)
    call(self.onSwipe, direction, info)
    if self.byDirection then call(self.byDirection[direction], info) end
end

function Recognizer:canStartAt(x, y)
    if self.anywhere or not self.areaTester then return true end
    return self.areaTester(x, y) and true or false
end

-- Returns true if it tracks this touch (the value the engine wants from "began").
function Recognizer:began(x, y)
    if not self.enabled or self.tracking then return false end
    if not self:canStartAt(x, y) then return false end

    self.tracking = true
    self.fired = false
    self.startX, self.startY = x, y
    call(self.onBegan, x, y)
    return true
end

function Recognizer:moved(x, y)
    if not self.tracking then return end

    local dx, dy = x - self.startX, y - self.startY
    if self.onDrag then call(self.onDrag, dx, dy, self:makeInfo(nil, x, y)) end

    if self.fireOn == 'moved' and not self.fired then
        local direction = self:resolve(dx, dy)
        if direction then
            self.fired = true
            self:emit(direction, x, y)
        end
    end
end

function Recognizer:ended(x, y)
    if not self.tracking then return end
    self.tracking = false

    if self.fired then return end -- fireOn = 'moved' has already reported it

    local direction = self:resolve(x - self.startX, y - self.startY)
    if direction then
        self:emit(direction, x, y)
    else
        call(self.onTap, x, y)
    end
end

function Recognizer:cancelled()
    if not self.tracking then return end
    self.tracking = false
    call(self.onCancel)
end

---------------------------------------------------------------------------
-- Wiring to the engine
---------------------------------------------------------------------------

-- Adds a helper layer to `parent` that listens for swipes. A layer of its own is
-- used so that whatever touch handler `parent` already has is left alone.
-- Returns a handle: setEnabled(bool), isEnabled(), detach(), recognizer.
function swipe.attach(parent, options)
    assert(parent, 'swipe.attach: parent is nil')
    options = options or {}

    local recognizer = swipe.newRecognizer(options)

    local layer = CCLayer:create()
    layer:registerScriptTouchHandler(function(event, x, y)
        if event == 'began' then
            return recognizer:began(x, y)
        elseif event == 'moved' then
            recognizer:moved(x, y)
        elseif event == 'ended' then
            recognizer:ended(x, y)
        else
            recognizer:cancelled()
        end
        return true
    end, false, options.priority or swipe.defaults.priority, options.swallow == true)
    layer:setTouchEnabled(true)
    parent:addChild(layer, options.zOrder or swipe.defaults.zOrder)

    local handle = {recognizer = recognizer, layer = layer}

    function handle:setEnabled(enabled)
        recognizer.enabled = enabled and true or false
        if not recognizer.enabled then recognizer:cancelled() end
    end

    function handle:isEnabled() return recognizer.enabled end

    function handle:detach()
        recognizer:cancelled()
        layer:unregisterScriptTouchHandler()
        layer:removeFromParentAndCleanup(true)
    end

    return handle
end

return swipe
