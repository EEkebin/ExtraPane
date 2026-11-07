package io.github.eekebin.extrapane;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Point;
import android.graphics.drawable.GradientDrawable;
import android.net.*;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import java.net.*;
import java.util.ArrayList;

public final class MainActivity extends Activity implements SurfaceHolder.Callback, android.hardware.input.InputManager.InputDeviceListener {
    private final int background=Color.rgb(14,21,33),panel=Color.rgb(25,37,54),white=Color.rgb(233,241,250),muted=Color.rgb(165,185,207),accent=Color.rgb(112,209,247);
    private final Handler handler=new Handler();
    private final InputQueue queue=new InputQueue();
    private final RemoteInput input=new RemoteInput(queue);
    private ClientSession session;
    private FrameLayout root,videoArea;
    private SurfaceView video;
    private LinearLayout setup,toolbar;
    private TextView status,stats,capabilities;
    private EditText server,port,accessCode;
    private Spinner routes;
    private Spinner cameraChoices;private final ArrayList<CameraCapture.Choice> cameras=new ArrayList<>();
    private Spinner microphoneChoices;private final ArrayList<MicrophoneCapture.Choice> microphones=new ArrayList<>();
    private boolean microphoneBound,microphoneRunning;private MicrophoneShareService.LocalBinder microphoneBinder;
    private final android.content.ServiceConnection microphoneConnection=new android.content.ServiceConnection(){
        public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){microphoneBinder=(MicrophoneShareService.LocalBinder)binder;microphoneBinder.listen((text,running)->{microphoneRunning=running;if(running||(!cameraRunning&&session==null))status.setText(text);});}
        public void onServiceDisconnected(android.content.ComponentName name){microphoneBinder=null;microphoneRunning=false;}
    };
    private boolean cameraBound,cameraRunning,visible;private CameraShareService.LocalBinder cameraBinder;
    private final android.content.ServiceConnection cameraConnection=new android.content.ServiceConnection(){
        public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){cameraBinder=(CameraShareService.LocalBinder)binder;cameraBinder.listen((text,running)->{cameraRunning=running;if(running||(!microphoneRunning&&session==null))status.setText(text);});}
        public void onServiceDisconnected(android.content.ComponentName name){cameraBinder=null;cameraRunning=false;}
    };
    private boolean surfaceReady,showStats,paused;
    private int streamWidth=16,streamHeight=9;
    private final ArrayList<Route> choices=new ArrayList<>();
    private static final class Route {String label,address;Network network;Route(String l,String a,Network n){label=l;address=a;network=n;}}
    private final Runnable hideToolbar=()->{if(session!=null && !paused){toolbar.setVisibility(View.GONE);input.enable(true);}};
    @Override public void onCreate(Bundle state){super.onCreate(state);getWindow().setDecorFitsSystemWindows(false);build();getSystemService(android.hardware.input.InputManager.class).registerInputDeviceListener(this,handler);if(Build.VERSION.SDK_INT>=33)getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,this::back);}
    public void onInputDeviceAdded(int id){}
    public void onInputDeviceChanged(int id){input.reset();}
    public void onInputDeviceRemoved(int id){input.reset();}
    private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private GradientDrawable box(int color){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(14));return d;}
    private TextView text(String content,int size,int color){TextView t=new TextView(this);t.setText(content);t.setTextSize(size);t.setTextColor(color);t.setPadding(dp(4),dp(7),dp(4),dp(7));return t;}
    private Button button(String label,Runnable action){Button b=new Button(this);b.setText(label);b.setTextColor(white);b.setAllCaps(false);b.setBackground(box(panel));b.setOnClickListener(v->action.run());return b;}
    private void build(){
        root=new FrameLayout(this);root.setBackgroundColor(background);setContentView(root);
        root.setOnApplyWindowInsetsListener((v,insets)->{Insets i=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());root.setPadding(i.left,i.top,i.right,i.bottom);return insets;});
        videoArea=new FrameLayout(this);root.addView(videoArea,new FrameLayout.LayoutParams(-1,-1));
        video=new SurfaceView(this);video.getHolder().addCallback(this);video.setFocusableInTouchMode(true);
        videoArea.addView(video,new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER));
        video.setOnTouchListener((v,e)->input.motion(v,e));video.setOnGenericMotionListener((v,e)->input.motion(v,e));
        videoArea.addOnLayoutChangeListener((v,a,b,c,d,e,f,g,h)->fit());
        setup=new LinearLayout(this);setup.setOrientation(LinearLayout.VERTICAL);setup.setPadding(dp(24),dp(20),dp(24),dp(16));setup.setBackgroundColor(background);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.addView(setup);root.addView(scroll,new FrameLayout.LayoutParams(-1,-1));setup.setTag(scroll);
        setup.addView(text("ExtraPane",32,white));setup.addView(text("Another screen for your desktop",17,muted));
        setup.addView(text("Connect to a server",22,white));
        server=new EditText(this);server.setSingleLine();server.setTextColor(white);server.setHintTextColor(muted);server.setHint("Server IPv4 address");server.setInputType(android.text.InputType.TYPE_CLASS_PHONE);
        server.setText(getPreferences(0).getString("server", ""));setup.addView(server);
        port=new EditText(this);port.setSingleLine();port.setTextColor(white);port.setHintTextColor(muted);port.setHint("TCP port");port.setText("43824");port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);setup.addView(port);
        accessCode=new EditText(this);accessCode.setSingleLine();accessCode.setTextColor(white);accessCode.setHintTextColor(muted);accessCode.setHint("Access code (optional for trusted LAN)");accessCode.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD);accessCode.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(6)});accessCode.setSaveEnabled(false);setup.addView(accessCode);
        setup.addView(text("Connect through",14,muted));routes=new Spinner(this);setup.addView(routes);refreshRoutes();
        setup.addView(button("Refresh network addresses",this::refreshRoutes));
        setup.addView(button("Connect",this::connect));status=text("Ready",14,accent);setup.addView(status);
        setup.addView(text("Share a camera",22,white));cameraChoices=new Spinner(this);setup.addView(cameraChoices);refreshCameras();
        setup.addView(button("Refresh cameras",this::refreshCameras));setup.addView(button("Share selected camera",this::shareCamera));
        setup.addView(button("Stop camera sharing",()->startService(new android.content.Intent(this,CameraShareService.class).setAction(CameraShareService.STOP))));
        setup.addView(text("Camera-only preview: connect to the server's camera listener port. Hold the phone in landscape; automatic rotation is not implemented. Camera sharing continues in a foreground service until you stop it or Android withdraws access.",14,muted));
        setup.addView(text("Share a microphone",22,white));microphoneChoices=new Spinner(this);setup.addView(microphoneChoices);refreshMicrophones();
        setup.addView(button("Refresh microphones",this::refreshMicrophones));setup.addView(button("Share selected microphone",this::shareMicrophone));
        setup.addView(button("Mute microphone",()->microphoneAction(MicrophoneShareService.MUTE)));
        setup.addView(button("Unmute microphone",()->microphoneAction(MicrophoneShareService.UNMUTE)));
        setup.addView(button("Stop microphone sharing",()->microphoneAction(MicrophoneShareService.STOP)));
        setup.addView(text("Microphone-only preview: use the server's microphone listener port. Sends 48 kHz mono PCM audio, with a foreground notification containing Mute and Stop. Android may deny a selected input or silence recording when another app uses it.",14,muted));
        setup.addView(text("Encrypted LAN connection. Peer identity is not verified; use a trusted network.",14,muted));
        setup.addView(text("Touch, mouse, USB/Bluetooth keyboard and compatible active stylus input. On-screen keyboard, display system-audio playback and Android hosting are not available yet. Display, camera and microphone are separate session modes in this preview.",14,muted));
        setup.addView(button("About",()->new AlertDialog.Builder(this).setTitle("ExtraPane").setMessage("Running version: "+BuildConfig.VERSION_NAME+"\nAndroid client development build\n\nC++ protocol • Android MediaCodec\nA project-wide license has not been selected.").setPositiveButton("GitHub",(d,w)->startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW,Uri.parse("https://github.com/EEkebin/ExtraPane")))).setNegativeButton("Close",null).show()));
        toolbar=new LinearLayout(this);toolbar.setGravity(Gravity.CENTER);toolbar.setPadding(dp(8),dp(4),dp(8),dp(4));toolbar.setBackground(box(panel));
        toolbar.addView(button("Settings",this::settings));toolbar.addView(button("Hide",()->{paused=false;hideToolbar.run();}));toolbar.addView(button("Disconnect",this::disconnect));
        FrameLayout.LayoutParams top=new FrameLayout.LayoutParams(-2,dp(56),Gravity.TOP|Gravity.CENTER_HORIZONTAL);top.topMargin=dp(12);root.addView(toolbar,top);toolbar.setVisibility(View.GONE);
        TextView handle=text("―",18,accent);handle.setGravity(Gravity.CENTER);FrameLayout.LayoutParams hp=new FrameLayout.LayoutParams(dp(68),dp(24),Gravity.TOP|Gravity.CENTER_HORIZONTAL);root.addView(handle,hp);
        handle.setOnTouchListener((v,e)->{if(StylusInput.isPen(e.getToolType(0)))return false;if(session!=null){reveal();return true;}return false;});
        stats=text("",12,white);stats.setBackground(box(panel));FrameLayout.LayoutParams sp=new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.START);sp.setMargins(dp(12),0,dp(12),dp(12));root.addView(stats,sp);stats.setVisibility(View.GONE);
        capabilities=text("",13,muted);
    }
    private void refreshRoutes(){
        choices.clear();choices.add(new Route("Automatic route","",null));
        ConnectivityManager cm=(ConnectivityManager)getSystemService(CONNECTIVITY_SERVICE);
        for(Network network:cm.getAllNetworks()){
            NetworkCapabilities caps=cm.getNetworkCapabilities(network);LinkProperties links=cm.getLinkProperties(network);
            if(caps==null || links==null || (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) && !caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)))continue;
            for(LinkAddress link:links.getLinkAddresses())if(link.getAddress() instanceof Inet4Address && !link.getAddress().isLoopbackAddress() && !link.getAddress().isAnyLocalAddress())choices.add(new Route(links.getInterfaceName()+" · "+link.getAddress().getHostAddress(),link.getAddress().getHostAddress(),network));
        }
        ArrayList<String> labels=new ArrayList<>();for(Route route:choices)labels.add(route.label);
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels);routes.setAdapter(adapter);
    }
    private void connect(){
        if(microphoneRunning||(microphoneBound&&microphoneBinder==null)){status.setText("Stop microphone sharing before starting the display");return;}
        if(cameraRunning||(cameraBound&&cameraBinder==null)){status.setText("Stop camera sharing before starting the display in this preview");return;}
        if(session!=null)return;if(!surfaceReady){status.setText("Video surface is not ready");return;}
        String address=server.getText().toString().trim();try{ClientSession.ipv4(address);}catch(Exception e){status.setText(e.getMessage());return;}
        int selectedPort;try{selectedPort=Integer.parseInt(port.getText().toString());if(selectedPort<1||selectedPort>65535)throw new NumberFormatException();}catch(NumberFormatException e){status.setText("Enter a port from 1 to 65535");return;}
        String code=accessCode.getText().toString();if(!code.isEmpty()&&!code.matches("[0-9]{6}")){status.setText("Enter a six-digit access code");return;}
        getPreferences(0).edit().putString("server",address).apply();
        ((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(server.getWindowToken(),0);
        Point size=new Point();getDisplay().getRealSize(size);int hz=Math.max(1,Math.min(60,Math.round(getDisplay().getRefreshRate())));
        Route route=choices.get(routes.getSelectedItemPosition());paused=false;
        ClientSession next=new ClientSession(address,route.address,route.network,video.getHolder().getSurface(),size.x,size.y,hz,15,0,queue,new ClientSession.Listener(){
            public void status(String message){runOnUiThread(()->status.setText(message));}
            public void format(int w,int h,int caps,String decoder){runOnUiThread(()->{streamWidth=w;streamHeight=h;fit();capabilities.setText("Server input: "+((caps&4)!=0?"touch ":"")+((caps&1)!=0?"mouse ":"")+((caps&2)!=0?"keyboard ":"")+((caps&8)!=0?"pen":"\nPen forwarding unavailable: this host did not grant pen input.")+"\nDecoder: "+decoder);input.enable(!paused && toolbar.getVisibility()!=View.VISIBLE);});}
            public void statistics(String text){runOnUiThread(()->stats.setText(text));}
            public void ended(String reason){runOnUiThread(()->{session=null;input.enable(false);handler.removeCallbacks(hideToolbar);((View)setup.getTag()).setVisibility(View.VISIBLE);toolbar.setVisibility(View.GONE);stats.setVisibility(View.GONE);status.setText(reason);getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);getWindow().getInsetsController().show(WindowInsets.Type.systemBars());});}
        },selectedPort,code);accessCode.setText("");
        session=next;((View)setup.getTag()).setVisibility(View.GONE);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getInsetsController().setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);getWindow().getInsetsController().hide(WindowInsets.Type.systemBars());
        stats.setVisibility(showStats?View.VISIBLE:View.GONE);video.requestFocus();reveal();next.start();
    }
    private void refreshCameras(){
        cameras.clear();try{cameras.addAll(CameraCapture.choices(this));}catch(Exception error){if(status!=null)status.setText("Camera inventory unavailable: "+error.getMessage());}
        ArrayList<String> labels=new ArrayList<>();for(CameraCapture.Choice camera:cameras)labels.add(camera.toString());if(labels.isEmpty())labels.add("No camera supports a compatible H.264 mode");
        cameraChoices.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels));
    }
    private void bindCamera(){if(!cameraBound){cameraBound=bindService(new android.content.Intent(this,CameraShareService.class),cameraConnection,0);if(!cameraBound)cameraRunning=false;}}
    private void shareCamera(){
        if(microphoneRunning||(microphoneBound&&microphoneBinder==null)){status.setText("Stop microphone sharing before starting a camera");return;}
        if(!visible||session!=null||cameraRunning||(cameraBound&&cameraBinder==null)){status.setText("Stop the current session before sharing a camera");return;}
        if(checkSelfPermission(android.Manifest.permission.CAMERA)!=android.content.pm.PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{android.Manifest.permission.CAMERA},41);return;}
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED){
            // Notification denial does not remove Android's foreground-service
            // task-manager indicator or the in-app Stop control.
            if(!getPreferences(0).getBoolean("cameraNotificationAsked",false)){getPreferences(0).edit().putBoolean("cameraNotificationAsked",true).apply();requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},42);return;}
        }
        if(cameras.isEmpty()){refreshCameras();if(cameras.isEmpty()){status.setText("No supported camera is available");return;}}
        String address=server.getText().toString().trim();try{ClientSession.ipv4(address);}catch(Exception error){status.setText(error.getMessage());return;}
        int number;try{number=Integer.parseInt(port.getText().toString());if(number<1||number>65535)throw new NumberFormatException();}catch(NumberFormatException error){status.setText("Enter a port from 1 to 65535");return;}
        String code=accessCode.getText().toString();if(!code.isEmpty()&&!code.matches("[0-9]{6}")){status.setText("Enter a six-digit access code");return;}
        Route route=choices.get(routes.getSelectedItemPosition());CameraCapture.Choice camera=cameras.get(cameraChoices.getSelectedItemPosition());
        android.content.Intent intent=new android.content.Intent(this,CameraShareService.class).putExtra("address",address).putExtra("port",number).putExtra("local",route.address).putExtra("network",route.network).putExtra("code",code)
            .putExtra("camera",camera.id()).putExtra("cameraLabel",camera.label()).putExtra("width",camera.width()).putExtra("height",camera.height()).putExtra("fps",camera.fps());
        try{startForegroundService(intent);accessCode.setText("");cameraRunning=true;status.setText("Starting camera sharing…");bindCamera();}catch(RuntimeException error){cameraRunning=false;status.setText("Cannot start camera sharing: "+error.getMessage());}
    }
    private void refreshMicrophones(){
        microphones.clear();try{microphones.addAll(MicrophoneCapture.choices(this));}catch(RuntimeException error){status.setText("Microphone inventory unavailable: "+error.getMessage());}
        ArrayList<String> labels=new ArrayList<>();for(MicrophoneCapture.Choice microphone:microphones)labels.add(microphone.label());if(labels.isEmpty())labels.add("No microphone input is connected");
        microphoneChoices.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels));
    }
    private void bindMicrophone(){if(!microphoneBound){microphoneBound=bindService(new android.content.Intent(this,MicrophoneShareService.class),microphoneConnection,0);if(!microphoneBound)microphoneRunning=false;}}
    private void microphoneAction(String action){startService(new android.content.Intent(this,MicrophoneShareService.class).setAction(action));}
    private void shareMicrophone(){
        if(!visible||session!=null||cameraRunning||microphoneRunning||(cameraBound&&cameraBinder==null)||(microphoneBound&&microphoneBinder==null)){status.setText("Stop the current session before sharing a microphone");return;}
        if(checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)!=android.content.pm.PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{android.Manifest.permission.RECORD_AUDIO},43);return;}
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED&&!getPreferences(0).getBoolean("microphoneNotificationAsked",false)){
            getPreferences(0).edit().putBoolean("microphoneNotificationAsked",true).apply();requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},44);return;
        }
        if(microphones.isEmpty()){refreshMicrophones();if(microphones.isEmpty()){status.setText("No microphone is available");return;}}
        String address=server.getText().toString().trim();try{ClientSession.ipv4(address);}catch(Exception error){status.setText(error.getMessage());return;}
        int number;try{number=Integer.parseInt(port.getText().toString());if(number<1||number>65535)throw new NumberFormatException();}catch(NumberFormatException error){status.setText("Enter a port from 1 to 65535");return;}
        String code=accessCode.getText().toString();if(!code.isEmpty()&&!code.matches("[0-9]{6}")){status.setText("Enter a six-digit access code");return;}
        Route route=choices.get(routes.getSelectedItemPosition());MicrophoneCapture.Choice microphone=microphones.get(microphoneChoices.getSelectedItemPosition());
        android.content.Intent intent=new android.content.Intent(this,MicrophoneShareService.class).putExtra("address",address).putExtra("port",number).putExtra("local",route.address).putExtra("network",route.network).putExtra("code",code)
            .putExtra("microphone",microphone.id()).putExtra("microphoneLabel",microphone.label());
        try{startForegroundService(intent);accessCode.setText("");microphoneRunning=true;status.setText("Starting microphone sharing...");bindMicrophone();}catch(RuntimeException error){microphoneRunning=false;status.setText("Cannot start microphone sharing: "+error.getMessage());}
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request==41){if(grants.length>0&&grants[0]==android.content.pm.PackageManager.PERMISSION_GRANTED)shareCamera();else status.setText("Camera permission was denied");}else if(request==42)shareCamera();else if(request==43){if(grants.length>0&&grants[0]==android.content.pm.PackageManager.PERMISSION_GRANTED)shareMicrophone();else status.setText("Microphone permission was denied");}else if(request==44)shareMicrophone();}
    @Override protected void onStart(){super.onStart();visible=true;bindCamera();bindMicrophone();}
    private void fit(){int w=videoArea.getWidth(),h=videoArea.getHeight();if(w==0 || h==0)return;double scale=Math.min(w/(double)streamWidth,h/(double)streamHeight);FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)video.getLayoutParams();int nw=(int)(streamWidth*scale),nh=(int)(streamHeight*scale);if(p.width!=nw || p.height!=nh){p.width=nw;p.height=nh;p.gravity=Gravity.CENTER;video.setLayoutParams(p);}}
    private void reveal(){if(session==null)return;input.enable(false);toolbar.setVisibility(View.VISIBLE);handler.removeCallbacks(hideToolbar);if(!paused)handler.postDelayed(hideToolbar,5000);}
    private void settings(){paused=true;input.enable(false);handler.removeCallbacks(hideToolbar);new AlertDialog.Builder(this).setTitle("Client settings").setMessage(capabilities.getText()+"\n\nStylus: contact, hover, pressure, tilt, primary barrel button and eraser require support from the device and host. Barrel rotation and secondary stylus buttons are not forwarded. A passive capacitive stylus is touch input.\n\nUSB/Bluetooth keyboards use the host keyboard layout. IME composition and system-reserved keys are not forwarded.\n\nStatistics show received data and submitted decoder output, not end-to-end latency.")
        .setPositiveButton(showStats?"Hide statistics":"Show statistics",(d,w)->{showStats=!showStats;stats.setVisibility(showStats?View.VISIBLE:View.GONE);paused=false;reveal();})
        .setNegativeButton("Resume",(d,w)->{paused=false;reveal();}).setOnCancelListener(d->{paused=false;reveal();}).show();}
    private void disconnect(){input.enable(false);ClientSession s=session;if(s!=null){status.setText("Disconnecting…");s.cancel();}}
    private void back(){if(session!=null){paused=true;reveal();}else finish();}
    @SuppressWarnings("deprecation")
    @android.annotation.SuppressLint("GestureBackNavigation") // API 30-32 fallback; 33+ uses OnBackInvokedDispatcher above.
    @Override public void onBackPressed(){back();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(session!=null){int code=event.getKeyCode();if(code==KeyEvent.KEYCODE_F10 || code==KeyEvent.KEYCODE_F8 || (code==KeyEvent.KEYCODE_Q && event.isCtrlPressed() && event.isShiftPressed())){if(event.getAction()==KeyEvent.ACTION_DOWN){input.reset();if(code==KeyEvent.KEYCODE_Q)disconnect();else{paused=true;reveal();}}return true;}if(input.key(event))return true;}return super.dispatchKeyEvent(event);
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus)input.enable(false);else if(session!=null)input.enable(!paused && toolbar.getVisibility()!=View.VISIBLE);}
    @Override protected void onStop(){visible=false;disconnect();if(cameraBinder!=null){cameraBinder.listen(null);cameraBinder=null;}if(cameraBound){unbindService(cameraConnection);cameraBound=false;}if(microphoneBinder!=null){microphoneBinder.listen(null);microphoneBinder=null;}if(microphoneBound){unbindService(microphoneConnection);microphoneBound=false;}super.onStop();}
    @Override protected void onDestroy(){disconnect();getSystemService(android.hardware.input.InputManager.class).unregisterInputDeviceListener(this);handler.removeCallbacksAndMessages(null);super.onDestroy();}
    public void surfaceCreated(SurfaceHolder holder){surfaceReady=true;}
    public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    public void surfaceDestroyed(SurfaceHolder holder){surfaceReady=false;disconnect();}
}
