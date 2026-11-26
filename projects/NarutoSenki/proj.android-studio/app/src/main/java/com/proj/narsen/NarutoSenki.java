package com.proj.narsen;

import android.content.Context;
import android.os.Bundle;
import android.view.WindowManager;

import org.cocos2dx.lib.Cocos2dxActivity;
import org.cocos2dx.lib.Cocos2dxGLSurfaceView;

public class NarutoSenki extends Cocos2dxActivity{
	protected void onCreate(Bundle savedInstanceState){
		CrashReporter.install(this);
		super.onCreate(savedInstanceState);
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON, WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
	}

	public Cocos2dxGLSurfaceView onCreateGLSurfaceView() {
		return new LuaGLSurfaceView(this);
	}

	static {
		System.loadLibrary("cocos2dcpp");
	}
}

class LuaGLSurfaceView extends Cocos2dxGLSurfaceView {

	public LuaGLSurfaceView(Context context) {
		super(context);
	}

	// NOTE: no onKeyDown() override on purpose. KEYCODE_BACK must reach Cocos2dxGLSurfaceView's own
	// onKeyDown() so it is queued to the C++ side (Layer::keyBackClicked: StartMenu, SelectLayer, ...).
	// V22 force-killed the process here instead, which is why this was dropped in V2.
}
