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
    private LinearLayout setup,toolbar,destinationFields;
    private ResourceCard displayCard;private Button disconnectAllButton;
    private TextView destinationSummary;private boolean displayConnected,displayStopping;
    private String displayTargetKey="";
    private String displayDestination="",cachedCode="",cachedDestination="";
    private final ArrayList<View> marked=new ArrayList<>();
    private final java.util.HashMap<View,android.graphics.drawable.Drawable> oldBackgrounds=new java.util.HashMap<>();
    private Spinner displayMode;private EditText displayWidth,displayHeight,displayFps;
    private LinearLayout customDisplay;
    private TextView status,stats,capabilities;
    private EditText server,port,accessCode;
    private Spinner routes;
    private boolean visible;
    private CaptureRows cameraRows,microphoneRows;private SpeakerRows speakerRows;private CheckBox singleServer;
    private Runnable permissionGranted,permissionDenied;
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
    private EditText field(String hint,String value){EditText field=new EditText(this);field.setSingleLine();field.setTextColor(white);field.setHintTextColor(muted);field.setHint(hint);field.setText(value);return field;}
    private Button redButton(String label,Runnable action){Button b=button(label,action);b.setBackground(box(Color.rgb(179,38,53)));return b;}
    private final class RefreshSpinner extends Spinner {
        private final Runnable refresh;
        RefreshSpinner(Context context,Runnable refresh){super(context,Spinner.MODE_DROPDOWN);this.refresh=refresh;setMinimumHeight(dp(48));}
        @Override public boolean performClick(){refresh.run();return super.performClick();}
    }
    private LinearLayout group(String title){
        LinearLayout outer=new LinearLayout(this);outer.setOrientation(LinearLayout.VERTICAL);outer.setBackground(box(panel));outer.setPadding(dp(12),dp(4),dp(12),dp(12));
        LinearLayout.LayoutParams margins=new LinearLayout.LayoutParams(-1,-2);margins.topMargin=dp(12);setup.addView(outer,margins);
        TextView header=text("?  "+title,18,white);header.setGravity(Gravity.CENTER);header.setMinimumHeight(dp(48));outer.addView(header);
        LinearLayout body=new LinearLayout(this);body.setOrientation(LinearLayout.VERTICAL);outer.addView(body);
        header.setOnClickListener(v->{boolean open=body.getVisibility()!=View.VISIBLE;body.setVisibility(open?View.VISIBLE:View.GONE);header.setText((open?"?  ":"?  ")+title);});
        return body;
    }
    private final class ResourceCard {
        final LinearLayout body,configuration;final PowerButton enable;final TextView summary,status;
        ResourceCard(String title,String label,Runnable action){
            body=group(title);enable=new PowerButton(MainActivity.this,label,action);body.addView(enable);
            configuration=new LinearLayout(MainActivity.this);configuration.setOrientation(LinearLayout.VERTICAL);body.addView(configuration);
            summary=text("",14,accent);body.addView(summary);status=text("",13,muted);body.addView(status);
        }
        void state(boolean running,boolean requested,boolean connected,String target,String detail,boolean stopping){
            enable.state(running||requested,stopping);configuration.setVisibility(running?View.GONE:View.VISIBLE);
            summary.setVisibility(running?View.VISIBLE:View.GONE);summary.setText((stopping?"Stopping sharing with ":connected?"Connected to ":"Connecting to ")+target+(detail.isEmpty()?"":"\n"+detail));
        }
    }
    private void clearMarks(){for(View v:marked){v.setBackground(oldBackgrounds.get(v));if(v instanceof EditText)((EditText)v).setError(null);}marked.clear();oldBackgrounds.clear();}
    private void mark(View field){if(!marked.contains(field)){marked.add(field);oldBackgrounds.put(field,field.getBackground());}GradientDrawable border=box(background);border.setStroke(dp(2),Color.rgb(255,101,119));field.setBackground(border);}
    private final Runnable clearValidation=this::clearMarks;
    private void toast(String message){Toast.makeText(this,message,Toast.LENGTH_LONG).show();}
    private boolean validate(String resource){
        clearMarks();handler.removeCallbacks(clearValidation);
        if(!ConnectionValidation.ipv4(server.getText().toString().trim()))mark(server);
        if(!ConnectionValidation.port(port.getText().toString()))mark(port);
        if(!ConnectionValidation.code(accessCode.getText().toString()))mark(accessCode);
        if(routes.getSelectedItemPosition()<0||routes.getSelectedItemPosition()>=choices.size())mark(routes);
        if(resource.equals("display")&&displayMode.getSelectedItemPosition()==1){
            int w=ConnectionValidation.number(displayWidth.getText().toString()),h=ConnectionValidation.number(displayHeight.getText().toString()),fps=ConnectionValidation.number(displayFps.getText().toString());
            if(!ConnectionValidation.dimension(w))mark(displayWidth);if(!ConnectionValidation.dimension(h))mark(displayHeight);
            if((long)w*h>8847360){mark(displayWidth);mark(displayHeight);}if(fps<1||fps>360)mark(displayFps);
        }
        if(marked.isEmpty())return true;
        destinationFields.setVisibility(View.VISIBLE);toast("Check the highlighted fields to connect.");handler.postDelayed(clearValidation,5000);return false;
    }
    private String targetKey(){
        int index=routes.getSelectedItemPosition();if(index<0||index>=choices.size())return "";
        Route route=choices.get(index);return server.getText().toString().trim()+":"+port.getText()+"@"+route.address+"/"+route.network;
    }
    private String connectionCode(){
        Route route=choices.get(routes.getSelectedItemPosition());String destination=server.getText().toString().trim()+":"+port.getText()+"@"+route.address+"/"+route.network;
        String supplied=accessCode.getText().toString();if(!supplied.isEmpty()){cachedCode=supplied;cachedDestination=destination;}
        return destination.equals(cachedDestination)?cachedCode:supplied;
    }
    private void admissionError(String reason){if(reason.toLowerCase(java.util.Locale.ROOT).matches(".*(access code|authentication|server access|admission).*")){cachedCode="";cachedDestination="";accessCode.setText("");mark(accessCode);destinationFields.setVisibility(View.VISIBLE);toast("Check the highlighted fields to connect.");handler.removeCallbacks(clearValidation);handler.postDelayed(clearValidation,5000);}}
    private CaptureRows.Target captureTarget(){
        int index=routes.getSelectedItemPosition();Route route=index>=0&&index<choices.size()?choices.get(index):new Route("","",null);
        String address=server.getText().toString().trim();int number=ConnectionValidation.number(port.getText().toString());
        String code=ConnectionValidation.ipv4(address)&&number>0&&index>=0&&index<choices.size()?connectionCode():accessCode.getText().toString();
        return new CaptureRows.Target(address,number,route.address,route.network,code);
    }
    private void requestCapturePermission(boolean camera,Runnable granted,Runnable denied){
        if(!visible||permissionGranted!=null){toast("Finish the current permission request first.");denied.run();return;}
        String required=camera?android.Manifest.permission.CAMERA:android.Manifest.permission.RECORD_AUDIO;
        Runnable ready=()->requestCaptureNotification(granted,denied);
        if(checkSelfPermission(required)==android.content.pm.PackageManager.PERMISSION_GRANTED){ready.run();return;}
        permissionGranted=ready;permissionDenied=denied;
        requestPermissions(new String[]{required},camera?51:52);
    }
    private void requestCaptureNotification(Runnable granted,Runnable denied){
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED&&!getPreferences(0).getBoolean("captureNotificationsAsked",false)){
            getPreferences(0).edit().putBoolean("captureNotificationsAsked",true).apply();permissionGranted=()->{granted.run();accessCode.setText("");};permissionDenied=denied;
            requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},53);return;
        }
        granted.run();accessCode.setText("");
    }
    private void requestPlaybackNotification(Runnable granted,Runnable denied){
        if(!visible||permissionGranted!=null){toast("Finish the current permission request first.");denied.run();return;}
        requestCaptureNotification(granted,denied);
    }
    private void refreshState(){
        if(displayCard==null||cameraRows==null||microphoneRows==null||speakerRows==null)return;
        displayCard.state(session!=null,false,displayConnected,displayDestination,streamWidth+" x "+streamHeight+" @ "+(displayCard.summary.getTag()==null?"":displayCard.summary.getTag()),displayStopping);
        cameraRows.refreshLayout();speakerRows.refreshLayout();microphoneRows.refreshLayout();
        boolean capture=cameraRows.active()||speakerRows.active()||microphoneRows.active();
        disconnectAllButton.setVisibility(session!=null||capture?View.VISIBLE:View.GONE);
        boolean matching=(!displayConnected||targetKey().equals(displayTargetKey))&&cameraRows.matches(captureTarget())&&speakerRows.matches(captureTarget())&&microphoneRows.matches(captureTarget());
        destinationFields.setVisibility((displayConnected||capture)&&singleServer.isChecked()&&matching&&marked.isEmpty()?View.GONE:View.VISIBLE);
        destinationSummary.setVisibility(displayConnected?View.VISIBLE:View.GONE);destinationSummary.setText("Display server: "+displayDestination);
        if(session==null&&!capture){cachedCode="";cachedDestination="";}
    }
    private void disconnectAll(){disconnect();cameraRows.stopAll();speakerRows.stopAll();microphoneRows.stopAll();}
    private void showResources(){input.enable(false);paused=true;handler.removeCallbacks(hideToolbar);toolbar.setVisibility(View.GONE);stats.setVisibility(View.GONE);((View)setup.getTag()).setVisibility(View.VISIBLE);getWindow().getInsetsController().show(WindowInsets.Type.systemBars());}
    private void showDisplay(){if(session==null)return;((View)setup.getTag()).setVisibility(View.GONE);paused=false;video.requestFocus();getWindow().getInsetsController().hide(WindowInsets.Type.systemBars());reveal();}
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
        setup.addView(text("ExtraPane",32,white));setup.addView(text("Share your devices",17,muted));
        setup.addView(button("Devices",()->{if(session!=null||cameraRows.active()||microphoneRows.active()||speakerRows.active()){Toast.makeText(this,"Disconnect active resources before opening Devices",Toast.LENGTH_LONG).show();return;}startActivity(new android.content.Intent(this,PeerActivity.class));finish();}));
        setup.addView(text("Client",24,white));
        singleServer=new CheckBox(this);singleServer.setText("Use one server for all resources");singleServer.setTextColor(white);singleServer.setChecked(true);singleServer.setOnCheckedChangeListener((b,value)->refreshState());setup.addView(singleServer);
        destinationFields=new LinearLayout(this);destinationFields.setOrientation(LinearLayout.VERTICAL);setup.addView(destinationFields);
        destinationFields.addView(text("Destination for new connections",16,white));
        server=field("Server IPv4 address",getPreferences(0).getString("server", ""));server.setInputType(android.text.InputType.TYPE_CLASS_PHONE);destinationFields.addView(server);
        port=field("TCP port","43824");port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);destinationFields.addView(port);
        accessCode=field("Access code (optional)","");accessCode.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD);accessCode.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(6)});accessCode.setSaveEnabled(false);destinationFields.addView(accessCode);
        destinationFields.addView(text("Connect through",14,muted));routes=new RefreshSpinner(this,this::refreshRoutes);destinationFields.addView(routes);refreshRoutes();
        destinationSummary=text("",15,accent);setup.addView(destinationSummary);
        disconnectAllButton=redButton("Disconnect all",this::disconnectAll);disconnectAllButton.setVisibility(View.GONE);setup.addView(disconnectAllButton);status=text("Ready",14,accent);setup.addView(status);
        displayCard=new ResourceCard("Displays","Display",()->{if(session==null)connect();else disconnect();});
        displayMode=new Spinner(this);displayMode.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Native phone display","Custom resolution and frame rate"}));displayCard.configuration.addView(displayMode);
        customDisplay=new LinearLayout(this);customDisplay.setOrientation(LinearLayout.VERTICAL);displayCard.configuration.addView(customDisplay);
        displayWidth=field("Width","1920");displayHeight=field("Height","1080");displayFps=field("Target FPS","60");
        for(EditText field:new EditText[]{displayWidth,displayHeight,displayFps}){field.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);customDisplay.addView(field);}
        displayMode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onItemSelected(AdapterView<?> p,View v,int position,long id){customDisplay.setVisibility(position==1?View.VISIBLE:View.GONE);}public void onNothingSelected(AdapterView<?> p){}});
        displayCard.body.addView(button("Show display",this::showDisplay));
        cameraRows=new CaptureRows(this,true,group("Cameras"),this::captureTarget,()->singleServer.isChecked(),this::requestCapturePermission,this::refreshState,()->validate("capture"),(common,reason)->{cachedCode="";cachedDestination="";if(common)admissionError(reason);});
        speakerRows=new SpeakerRows(this,group("Speakers"),this::captureTarget,()->singleServer.isChecked(),this::requestPlaybackNotification,this::refreshState,()->validate("capture"),(common,reason)->{cachedCode="";cachedDestination="";if(common)admissionError(reason);});
        microphoneRows=new CaptureRows(this,false,group("Microphones"),this::captureTarget,()->singleServer.isChecked(),this::requestCapturePermission,this::refreshState,()->validate("capture"),(common,reason)->{cachedCode="";cachedDestination="";if(common)admissionError(reason);});
        LinearLayout unsupported=group("USBs");CheckBox usb=new CheckBox(this);usb.setText("USB device sharing");usb.setTextColor(muted);usb.setEnabled(false);unsupported.addView(usb);unsupported.addView(text("Unavailable: Android USB/IP export and exclusive device return are not implemented. USB keyboards and mice still work as Android input devices.",14,muted));
        setup.addView(text("Server hosting is not available on Android. Display, camera, speakers and microphone can connect together to a compatible server; each capture depends on Android permissions and hardware.",14,muted));
        setup.addView(button("About",()->new AlertDialog.Builder(this).setTitle("ExtraPane").setMessage("Running version: "+BuildConfig.VERSION_NAME+"\n\nA project-wide license has not been selected.").setPositiveButton("GitHub",(d,w)->startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW,Uri.parse("https://github.com/EEkebin/ExtraPane")))).setNegativeButton("Close",null).show()));
        toolbar=new LinearLayout(this);toolbar.setGravity(Gravity.CENTER);toolbar.setPadding(dp(8),dp(4),dp(8),dp(4));toolbar.setBackground(box(panel));
        toolbar.addView(button("Settings",this::settings));toolbar.addView(button("Hide",()->{paused=false;hideToolbar.run();}));toolbar.addView(button("Resources",this::showResources));toolbar.addView(redButton("Disconnect",this::disconnect));
        FrameLayout.LayoutParams top=new FrameLayout.LayoutParams(-2,dp(56),Gravity.TOP|Gravity.CENTER_HORIZONTAL);top.topMargin=dp(12);root.addView(toolbar,top);toolbar.setVisibility(View.GONE);
        TextView handle=text("―",18,accent);handle.setGravity(Gravity.CENTER);FrameLayout.LayoutParams hp=new FrameLayout.LayoutParams(dp(68),dp(24),Gravity.TOP|Gravity.CENTER_HORIZONTAL);root.addView(handle,hp);
        handle.setOnTouchListener((v,e)->{if(StylusInput.isPen(e.getToolType(0)))return false;if(session!=null){reveal();return true;}return false;});
        stats=text("",12,white);stats.setBackground(box(panel));FrameLayout.LayoutParams sp=new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.START);sp.setMargins(dp(12),0,dp(12),dp(12));root.addView(stats,sp);stats.setVisibility(View.GONE);
        capabilities=text("",13,muted);refreshState();
    }
    private void refreshRoutes(){
        String previous=routes.getSelectedItemPosition()>=0&&routes.getSelectedItemPosition()<choices.size()?choices.get(routes.getSelectedItemPosition()).address:"";
        choices.clear();choices.add(new Route("Automatic route","",null));
        ConnectivityManager cm=(ConnectivityManager)getSystemService(CONNECTIVITY_SERVICE);
        for(Network network:cm.getAllNetworks()){
            NetworkCapabilities caps=cm.getNetworkCapabilities(network);LinkProperties links=cm.getLinkProperties(network);
            if(caps==null || links==null || (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) && !caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)))continue;
            for(LinkAddress link:links.getLinkAddresses())if(link.getAddress() instanceof Inet4Address && !link.getAddress().isLoopbackAddress() && !link.getAddress().isAnyLocalAddress())choices.add(new Route(links.getInterfaceName()+" · "+link.getAddress().getHostAddress(),link.getAddress().getHostAddress(),network));
        }
        ArrayList<String> labels=new ArrayList<>();for(Route route:choices)labels.add(route.label);
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels);routes.setAdapter(adapter);for(int i=0;i<choices.size();++i)if(choices.get(i).address.equals(previous)){routes.setSelection(i);break;}
    }
    private void connect(){
        if(session!=null)return;if(!surfaceReady){status.setText("Video surface is not ready");refreshState();return;}
        if(!validate("display")){refreshState();return;}
        String address=server.getText().toString().trim();int selectedPort=Integer.parseInt(port.getText().toString());String code=connectionCode();
        getPreferences(0).edit().putString("server",address).apply();
        ((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(server.getWindowToken(),0);
        Point size=new Point();getDisplay().getRealSize(size);int hz=Math.max(1,Math.min(60,Math.round(getDisplay().getRefreshRate())));
        if(displayMode.getSelectedItemPosition()==1){size.x=Integer.parseInt(displayWidth.getText().toString());size.y=Integer.parseInt(displayHeight.getText().toString());hz=Integer.parseInt(displayFps.getText().toString());}
        displayTargetKey=targetKey();displayDestination=address+":"+selectedPort;displayConnected=false;displayStopping=false;streamWidth=size.x;streamHeight=size.y;displayCard.summary.setTag(hz+" target FPS");
        Route route=choices.get(routes.getSelectedItemPosition());paused=false;
        ClientSession next=new ClientSession(address,route.address,route.network,video.getHolder().getSurface(),size.x,size.y,hz,15,0,queue,new ClientSession.Listener(){
            public void connected(){runOnUiThread(()->{if(session!=null&&!displayConnected){displayConnected=true;toast("Display connected to "+displayDestination);refreshState();}});}
            public void status(String message){runOnUiThread(()->{status.setText(message);displayCard.status.setText(message);});}
            public void format(int w,int h,int caps,String decoder){runOnUiThread(()->{streamWidth=w;streamHeight=h;fit();capabilities.setText("Server input: "+((caps&4)!=0?"touch ":"")+((caps&1)!=0?"mouse ":"")+((caps&2)!=0?"keyboard ":"")+((caps&8)!=0?"pen":"\nPen forwarding unavailable: this host did not grant pen input.")+"\nDecoder: "+decoder);input.enable(!paused && toolbar.getVisibility()!=View.VISIBLE);});}
            public void statistics(String text){runOnUiThread(()->stats.setText(text));}
            public void ended(String reason){runOnUiThread(()->{session=null;displayConnected=false;displayStopping=false;input.enable(false);handler.removeCallbacks(hideToolbar);((View)setup.getTag()).setVisibility(View.VISIBLE);toolbar.setVisibility(View.GONE);stats.setVisibility(View.GONE);status.setText(reason);displayCard.status.setText(reason);admissionError(reason);getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);getWindow().getInsetsController().show(WindowInsets.Type.systemBars());refreshState();});}
        },selectedPort,code,displayMode.getSelectedItemPosition()==1);accessCode.setText("");
        session=next;refreshState();((View)setup.getTag()).setVisibility(View.GONE);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getInsetsController().setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);getWindow().getInsetsController().hide(WindowInsets.Type.systemBars());
        stats.setVisibility(showStats?View.VISIBLE:View.GONE);video.requestFocus();reveal();next.start();
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){
        super.onRequestPermissionsResult(request,permissions,grants);
        if(request==51||request==52||request==53){Runnable accepted=permissionGranted,rejected=permissionDenied;permissionGranted=null;permissionDenied=null;if((request==53||(grants.length>0&&grants[0]==android.content.pm.PackageManager.PERMISSION_GRANTED))&&visible){if(accepted!=null)accepted.run();}else if(rejected!=null)rejected.run();}
    }
    @Override protected void onStart(){super.onStart();visible=true;cameraRows.resume();speakerRows.resume();microphoneRows.resume();}
    private void fit(){int w=videoArea.getWidth(),h=videoArea.getHeight();if(w==0 || h==0)return;double scale=Math.min(w/(double)streamWidth,h/(double)streamHeight);FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)video.getLayoutParams();int nw=(int)(streamWidth*scale),nh=(int)(streamHeight*scale);if(p.width!=nw || p.height!=nh){p.width=nw;p.height=nh;p.gravity=Gravity.CENTER;video.setLayoutParams(p);}}
    private void reveal(){if(session==null)return;input.enable(false);toolbar.setVisibility(View.VISIBLE);handler.removeCallbacks(hideToolbar);if(!paused)handler.postDelayed(hideToolbar,5000);}
    private void settings(){paused=true;input.enable(false);handler.removeCallbacks(hideToolbar);new AlertDialog.Builder(this).setTitle("Client settings").setMessage(capabilities.getText()+"\n\nStylus: contact, hover, pressure, tilt, primary barrel button and eraser require support from the device and host. Barrel rotation and secondary stylus buttons are not forwarded. A passive capacitive stylus is touch input.\n\nUSB/Bluetooth keyboards use the host keyboard layout. IME composition and system-reserved keys are not forwarded.\n\nStatistics show received data and submitted decoder output, not end-to-end latency.")
        .setPositiveButton(showStats?"Hide statistics":"Show statistics",(d,w)->{showStats=!showStats;stats.setVisibility(showStats?View.VISIBLE:View.GONE);paused=false;reveal();})
        .setNegativeButton("Resume",(d,w)->{paused=false;reveal();}).setOnCancelListener(d->{paused=false;reveal();}).show();}
    private void disconnect(){input.enable(false);ClientSession s=session;if(s!=null){displayStopping=true;status.setText("Disconnecting…");s.cancel();refreshState();}}
    private void back(){if(session!=null){showResources();}else finish();}
    @SuppressWarnings("deprecation")
    @android.annotation.SuppressLint("GestureBackNavigation") // API 30-32 fallback; 33+ uses OnBackInvokedDispatcher above.
    @Override public void onBackPressed(){back();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(session!=null){int code=event.getKeyCode();if(code==KeyEvent.KEYCODE_F10 || code==KeyEvent.KEYCODE_F8 || (code==KeyEvent.KEYCODE_Q && event.isCtrlPressed() && event.isShiftPressed())){if(event.getAction()==KeyEvent.ACTION_DOWN){input.reset();if(code==KeyEvent.KEYCODE_Q)disconnectAll();else{paused=true;reveal();}}return true;}if(input.key(event))return true;}return super.dispatchKeyEvent(event);
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus)input.enable(false);else if(session!=null)input.enable(!paused && toolbar.getVisibility()!=View.VISIBLE);}
    @Override protected void onStop(){visible=false;disconnect();cameraRows.pause();speakerRows.pause();microphoneRows.pause();super.onStop();}
    @Override protected void onDestroy(){disconnect();getSystemService(android.hardware.input.InputManager.class).unregisterInputDeviceListener(this);handler.removeCallbacksAndMessages(null);super.onDestroy();}
    public void surfaceCreated(SurfaceHolder holder){surfaceReady=true;}
    public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    public void surfaceDestroyed(SurfaceHolder holder){surfaceReady=false;disconnect();}
}
