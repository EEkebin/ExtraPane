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
        setup.addView(text("Encrypted LAN connection. Peer identity is not verified; use a trusted network.",14,muted));
        setup.addView(text("Touch, mouse, USB/Bluetooth keyboard and compatible active stylus input. On-screen keyboard, audio and Android hosting are not available yet.",14,muted));
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
    @Override protected void onStop(){disconnect();super.onStop();}
    @Override protected void onDestroy(){disconnect();getSystemService(android.hardware.input.InputManager.class).unregisterInputDeviceListener(this);handler.removeCallbacksAndMessages(null);super.onDestroy();}
    public void surfaceCreated(SurfaceHolder holder){surfaceReady=true;}
    public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    public void surfaceDestroyed(SurfaceHolder holder){surfaceReady=false;disconnect();}
}
