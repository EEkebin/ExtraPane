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
    private ResourceCard displayCard,cameraCard,microphoneCard;
    private TextView destinationSummary;private boolean changingChecks,displayConnected,cameraConnected,microphoneConnected,cameraRequested,microphoneRequested;
    private String displayDestination="",cameraDestination="",microphoneDestination="",cachedCode="",cachedDestination="";
    private final ArrayList<View> marked=new ArrayList<>();
    private final java.util.HashMap<View,android.graphics.drawable.Drawable> oldBackgrounds=new java.util.HashMap<>();
    private Spinner displayMode;private EditText displayWidth,displayHeight,displayFps;
    private LinearLayout customDisplay;private CheckBox microphoneMute;
    private TextView status,stats,capabilities;
    private EditText server,port,accessCode;
    private Spinner routes;
    private Spinner cameraChoices;private final ArrayList<CameraCapture.Choice> cameras=new ArrayList<>();
    private Spinner microphoneChoices;private final ArrayList<MicrophoneCapture.Choice> microphones=new ArrayList<>();
    private boolean microphoneBound,microphoneRunning;private MicrophoneShareService.LocalBinder microphoneBinder;
    private final android.content.ServiceConnection microphoneConnection=new android.content.ServiceConnection(){
        public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){microphoneBinder=(MicrophoneShareService.LocalBinder)binder;microphoneBinder.listen((text,running,connected,target,detail)->resourceChanged(false,text,running,connected,target,detail));}
        public void onServiceDisconnected(android.content.ComponentName name){microphoneBinder=null;microphoneRunning=false;microphoneConnected=false;microphoneRequested=false;refreshState();}
    };
    private boolean cameraBound,cameraRunning,visible;private CameraShareService.LocalBinder cameraBinder;
    private final android.content.ServiceConnection cameraConnection=new android.content.ServiceConnection(){
        public void onServiceConnected(android.content.ComponentName name,android.os.IBinder binder){cameraBinder=(CameraShareService.LocalBinder)binder;cameraBinder.listen((text,running,connected,target,detail)->resourceChanged(true,text,running,connected,target,detail));}
        public void onServiceDisconnected(android.content.ComponentName name){cameraBinder=null;cameraRunning=false;cameraConnected=false;cameraRequested=false;refreshState();}
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
        final LinearLayout body,configuration;final CheckBox enable;final TextView summary,status;final Button disconnect;
        ResourceCard(String title,String label,Runnable action){
            body=group(title);enable=new CheckBox(MainActivity.this);enable.setText(label);enable.setTextColor(white);body.addView(enable);
            enable.setOnCheckedChangeListener((b,checked)->{if(!changingChecks)action.run();});
            configuration=new LinearLayout(MainActivity.this);configuration.setOrientation(LinearLayout.VERTICAL);body.addView(configuration);
            summary=text("",14,accent);body.addView(summary);status=text("",13,muted);body.addView(status);disconnect=redButton("Disconnect",()->{});body.addView(disconnect);
        }
        void state(boolean running,boolean requested,boolean connected,String target,String detail){
            enable.setChecked(running||requested);configuration.setVisibility(running?View.GONE:View.VISIBLE);
            summary.setVisibility(running?View.VISIBLE:View.GONE);summary.setText((connected?"Connected to ":"Connecting to ")+target+(detail.isEmpty()?"":"\n"+detail));
            disconnect.setVisibility(running?View.VISIBLE:View.GONE);
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
        if(resource.equals("camera")&&(cameras.isEmpty()||cameraChoices.getSelectedItemPosition()<0||cameraChoices.getSelectedItemPosition()>=cameras.size()))mark(cameraChoices);
        if(resource.equals("microphone")&&(microphones.isEmpty()||microphoneChoices.getSelectedItemPosition()<0||microphoneChoices.getSelectedItemPosition()>=microphones.size()))mark(microphoneChoices);
        if(resource.equals("display")&&displayMode.getSelectedItemPosition()==1){
            int w=ConnectionValidation.number(displayWidth.getText().toString()),h=ConnectionValidation.number(displayHeight.getText().toString()),fps=ConnectionValidation.number(displayFps.getText().toString());
            if(!ConnectionValidation.dimension(w))mark(displayWidth);if(!ConnectionValidation.dimension(h))mark(displayHeight);
            if((long)w*h>8847360){mark(displayWidth);mark(displayHeight);}if(fps<1||fps>360)mark(displayFps);
        }
        if(marked.isEmpty())return true;
        destinationFields.setVisibility(View.VISIBLE);toast("Check the highlighted fields to connect.");handler.postDelayed(clearValidation,5000);return false;
    }
    private String connectionCode(){
        Route route=choices.get(routes.getSelectedItemPosition());String destination=server.getText().toString().trim()+":"+port.getText()+"@"+route.address+"/"+route.network;
        String supplied=accessCode.getText().toString();if(!supplied.isEmpty()){cachedCode=supplied;cachedDestination=destination;}
        return destination.equals(cachedDestination)?cachedCode:supplied;
    }
    private void resourceChanged(boolean camera,String text,boolean running,boolean connected,String target,String detail){
        boolean previously=camera?cameraConnected:microphoneConnected;
        if(camera){cameraRunning=running;cameraRequested=false;cameraConnected=connected;cameraDestination=target;cameraCard.summary.setTag(detail);cameraCard.status.setText(text);}
        else{microphoneRunning=running;microphoneRequested=false;microphoneConnected=connected;microphoneDestination=target;microphoneCard.summary.setTag(detail);microphoneCard.status.setText(text);}
        if(connected&&!previously&&visible)toast((camera?"Camera":"Microphone")+" connected to "+target);
        if(!running)admissionError(text);
        refreshState();
    }
    private void admissionError(String reason){if(reason.toLowerCase(java.util.Locale.ROOT).matches(".*(access code|authentication|server access|admission).*")){mark(accessCode);destinationFields.setVisibility(View.VISIBLE);toast("Check the highlighted fields to connect.");handler.removeCallbacks(clearValidation);handler.postDelayed(clearValidation,5000);}}
    private void refreshState(){
        if(displayCard==null||cameraCard==null||microphoneCard==null)return;
        changingChecks=true;
        displayCard.state(session!=null,false,displayConnected,displayDestination,streamWidth+" x "+streamHeight+" @ "+(displayCard.summary.getTag()==null?"":displayCard.summary.getTag()));
        cameraCard.state(cameraRunning,cameraRequested,cameraConnected,cameraDestination,cameraCard.summary.getTag()==null?"":cameraCard.summary.getTag().toString());
        microphoneCard.state(microphoneRunning,microphoneRequested,microphoneConnected,microphoneDestination,microphoneCard.summary.getTag()==null?"":microphoneCard.summary.getTag().toString());
        if(microphoneMute!=null){microphoneMute.setChecked(microphoneBinder!=null&&microphoneBinder.muted());microphoneMute.setEnabled(microphoneRunning);}
        changingChecks=false;
        boolean connected=displayConnected||cameraConnected||microphoneConnected;
        destinationFields.setVisibility(connected&&marked.isEmpty()?View.GONE:View.VISIBLE);destinationSummary.setVisibility(connected?View.VISIBLE:View.GONE);
        destinationSummary.setText("Server: "+(displayConnected?displayDestination:cameraConnected?cameraDestination:microphoneDestination));
        if(session==null&&!cameraRunning&&!microphoneRunning&&!cameraRequested&&!microphoneRequested){cachedCode="";cachedDestination="";}
    }
    private void stopCamera(){cameraRequested=false;if(cameraRunning)startService(new android.content.Intent(this,CameraShareService.class).setAction(CameraShareService.STOP));refreshState();}
    private void stopMicrophone(){microphoneRequested=false;if(microphoneRunning)microphoneAction(MicrophoneShareService.STOP);refreshState();}
    private void disconnectAll(){disconnect();stopCamera();stopMicrophone();}
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
        setup.addView(text("Client",24,white));
        destinationFields=new LinearLayout(this);destinationFields.setOrientation(LinearLayout.VERTICAL);setup.addView(destinationFields);
        server=field("Server IPv4 address",getPreferences(0).getString("server", ""));server.setInputType(android.text.InputType.TYPE_CLASS_PHONE);destinationFields.addView(server);
        port=field("TCP port","43824");port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);destinationFields.addView(port);
        accessCode=field("Access code (optional)","");accessCode.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD);accessCode.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(6)});accessCode.setSaveEnabled(false);destinationFields.addView(accessCode);
        destinationFields.addView(text("Connect through",14,muted));routes=new RefreshSpinner(this,this::refreshRoutes);destinationFields.addView(routes);refreshRoutes();
        destinationSummary=text("",15,accent);setup.addView(destinationSummary);
        setup.addView(redButton("Disconnect all",this::disconnectAll));status=text("Ready",14,accent);setup.addView(status);
        displayCard=new ResourceCard("Displays","Display",()->{if(displayCard.enable.isChecked())connect();else disconnect();});
        displayMode=new Spinner(this);displayMode.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Native phone display","Custom resolution and frame rate"}));displayCard.configuration.addView(displayMode);
        customDisplay=new LinearLayout(this);customDisplay.setOrientation(LinearLayout.VERTICAL);displayCard.configuration.addView(customDisplay);
        displayWidth=field("Width","1920");displayHeight=field("Height","1080");displayFps=field("Target FPS","60");
        for(EditText field:new EditText[]{displayWidth,displayHeight,displayFps}){field.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);customDisplay.addView(field);}
        displayMode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onItemSelected(AdapterView<?> p,View v,int position,long id){customDisplay.setVisibility(position==1?View.VISIBLE:View.GONE);}public void onNothingSelected(AdapterView<?> p){}});
        displayCard.body.addView(button("Show display",this::showDisplay));displayCard.disconnect.setOnClickListener(v->disconnect());
        cameraCard=new ResourceCard("Cameras","Camera",()->{if(cameraCard.enable.isChecked()){cameraRequested=true;shareCamera();}else{cameraRequested=false;stopCamera();}});
        cameraChoices=new RefreshSpinner(this,this::refreshCameras);cameraCard.configuration.addView(cameraChoices);refreshCameras();cameraCard.disconnect.setOnClickListener(v->stopCamera());
        microphoneCard=new ResourceCard("Microphones","Microphone",()->{if(microphoneCard.enable.isChecked()){microphoneRequested=true;shareMicrophone();}else{microphoneRequested=false;stopMicrophone();}});
        microphoneChoices=new RefreshSpinner(this,this::refreshMicrophones);microphoneCard.configuration.addView(microphoneChoices);refreshMicrophones();microphoneCard.disconnect.setOnClickListener(v->stopMicrophone());
        microphoneMute=new CheckBox(this);microphoneMute.setText("Mute microphone");microphoneMute.setTextColor(white);microphoneMute.setOnCheckedChangeListener((b,value)->{if(!changingChecks&&microphoneRunning)microphoneAction(value?MicrophoneShareService.MUTE:MicrophoneShareService.UNMUTE);});microphoneCard.body.addView(microphoneMute);
        LinearLayout unsupported=group("USBs");CheckBox usb=new CheckBox(this);usb.setText("USB device sharing");usb.setTextColor(muted);usb.setEnabled(false);unsupported.addView(usb);unsupported.addView(text("Unavailable: Android USB/IP export and exclusive device return are not implemented. USB keyboards and mice still work as Android input devices.",14,muted));
        setup.addView(text("Server hosting is not available on Android. Display, camera and microphone can connect together to a compatible server; each capture depends on Android permissions and hardware.",14,muted));
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
        displayDestination=address+":"+selectedPort;displayConnected=false;streamWidth=size.x;streamHeight=size.y;displayCard.summary.setTag(hz+" target FPS");
        Route route=choices.get(routes.getSelectedItemPosition());paused=false;
        ClientSession next=new ClientSession(address,route.address,route.network,video.getHolder().getSurface(),size.x,size.y,hz,15,0,queue,new ClientSession.Listener(){
            public void connected(){runOnUiThread(()->{if(session!=null&&!displayConnected){displayConnected=true;toast("Display connected to "+displayDestination);refreshState();}});}
            public void status(String message){runOnUiThread(()->{status.setText(message);displayCard.status.setText(message);});}
            public void format(int w,int h,int caps,String decoder){runOnUiThread(()->{streamWidth=w;streamHeight=h;fit();capabilities.setText("Server input: "+((caps&4)!=0?"touch ":"")+((caps&1)!=0?"mouse ":"")+((caps&2)!=0?"keyboard ":"")+((caps&8)!=0?"pen":"\nPen forwarding unavailable: this host did not grant pen input.")+"\nDecoder: "+decoder);input.enable(!paused && toolbar.getVisibility()!=View.VISIBLE);});}
            public void statistics(String text){runOnUiThread(()->stats.setText(text));}
            public void ended(String reason){runOnUiThread(()->{session=null;displayConnected=false;input.enable(false);handler.removeCallbacks(hideToolbar);((View)setup.getTag()).setVisibility(View.VISIBLE);toolbar.setVisibility(View.GONE);stats.setVisibility(View.GONE);status.setText(reason);displayCard.status.setText(reason);admissionError(reason);getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);getWindow().getInsetsController().show(WindowInsets.Type.systemBars());refreshState();});}
        },selectedPort,code,displayMode.getSelectedItemPosition()==1);accessCode.setText("");
        session=next;refreshState();((View)setup.getTag()).setVisibility(View.GONE);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getInsetsController().setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);getWindow().getInsetsController().hide(WindowInsets.Type.systemBars());
        stats.setVisibility(showStats?View.VISIBLE:View.GONE);video.requestFocus();reveal();next.start();
    }
    private void refreshCameras(){
        String previous=cameraChoices.getSelectedItemPosition()>=0&&cameraChoices.getSelectedItemPosition()<cameras.size()?cameras.get(cameraChoices.getSelectedItemPosition()).id():"";
        cameras.clear();try{cameras.addAll(CameraCapture.choices(this));}catch(Exception error){if(status!=null)status.setText("Camera inventory unavailable: "+error.getMessage());}
        ArrayList<String> labels=new ArrayList<>();for(CameraCapture.Choice camera:cameras)labels.add(camera.toString());if(labels.isEmpty())labels.add("No camera supports a compatible H.264 mode");
        cameraChoices.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels));for(int i=0;i<cameras.size();++i)if(cameras.get(i).id().equals(previous)){cameraChoices.setSelection(i);break;}
    }
    private void bindCamera(){if(!cameraBound){cameraBound=bindService(new android.content.Intent(this,CameraShareService.class),cameraConnection,0);if(!cameraBound)cameraRunning=false;}}
    private void shareCamera(){
        if(!visible||!cameraRequested)return;if(cameraRunning||(cameraBound&&cameraBinder==null))return;
        if(!validate("camera")){cameraRequested=false;refreshState();return;}
        if(checkSelfPermission(android.Manifest.permission.CAMERA)!=android.content.pm.PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{android.Manifest.permission.CAMERA},41);return;}
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED){
            // Notification denial does not remove Android's foreground-service
            // task-manager indicator or the in-app Stop control.
            if(!getPreferences(0).getBoolean("cameraNotificationAsked",false)){getPreferences(0).edit().putBoolean("cameraNotificationAsked",true).apply();requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},42);return;}
        }
        if(cameras.isEmpty())refreshCameras();
        String address=server.getText().toString().trim();int number=Integer.parseInt(port.getText().toString());String code=connectionCode();
        Route route=choices.get(routes.getSelectedItemPosition());CameraCapture.Choice camera=cameras.get(cameraChoices.getSelectedItemPosition());
        cameraDestination=address+":"+number;cameraCard.summary.setTag(camera.toString());
        android.content.Intent intent=new android.content.Intent(this,CameraShareService.class).putExtra("address",address).putExtra("port",number).putExtra("local",route.address).putExtra("network",route.network).putExtra("code",code)
            .putExtra("camera",camera.id()).putExtra("cameraLabel",camera.label()).putExtra("width",camera.width()).putExtra("height",camera.height()).putExtra("fps",camera.fps());
        try{startForegroundService(intent);accessCode.setText("");cameraRunning=true;refreshState();status.setText("Starting camera sharing…");bindCamera();}catch(RuntimeException error){cameraRunning=false;cameraRequested=false;refreshState();status.setText("Cannot start camera sharing: "+error.getMessage());}
    }
    private void refreshMicrophones(){
        int previous=microphoneChoices.getSelectedItemPosition()>=0&&microphoneChoices.getSelectedItemPosition()<microphones.size()?microphones.get(microphoneChoices.getSelectedItemPosition()).id():-1;
        microphones.clear();try{microphones.addAll(MicrophoneCapture.choices(this));}catch(RuntimeException error){status.setText("Microphone inventory unavailable: "+error.getMessage());}
        ArrayList<String> labels=new ArrayList<>();for(MicrophoneCapture.Choice microphone:microphones)labels.add(microphone.label());if(labels.isEmpty())labels.add("No microphone input is connected");
        microphoneChoices.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels));for(int i=0;i<microphones.size();++i)if(microphones.get(i).id()==previous){microphoneChoices.setSelection(i);break;}
    }
    private void bindMicrophone(){if(!microphoneBound){microphoneBound=bindService(new android.content.Intent(this,MicrophoneShareService.class),microphoneConnection,0);if(!microphoneBound)microphoneRunning=false;}}
    private void microphoneAction(String action){startService(new android.content.Intent(this,MicrophoneShareService.class).setAction(action));}
    private void shareMicrophone(){
        if(!visible||!microphoneRequested)return;if(microphoneRunning||(microphoneBound&&microphoneBinder==null))return;
        if(!validate("microphone")){microphoneRequested=false;refreshState();return;}
        if(checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)!=android.content.pm.PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{android.Manifest.permission.RECORD_AUDIO},43);return;}
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED&&!getPreferences(0).getBoolean("microphoneNotificationAsked",false)){
            getPreferences(0).edit().putBoolean("microphoneNotificationAsked",true).apply();requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},44);return;
        }
        if(microphones.isEmpty())refreshMicrophones();
        String address=server.getText().toString().trim();int number=Integer.parseInt(port.getText().toString());String code=connectionCode();
        Route route=choices.get(routes.getSelectedItemPosition());MicrophoneCapture.Choice microphone=microphones.get(microphoneChoices.getSelectedItemPosition());
        microphoneDestination=address+":"+number;microphoneCard.summary.setTag(microphone.label()+" - 48 kHz mono");
        android.content.Intent intent=new android.content.Intent(this,MicrophoneShareService.class).putExtra("address",address).putExtra("port",number).putExtra("local",route.address).putExtra("network",route.network).putExtra("code",code)
            .putExtra("microphone",microphone.id()).putExtra("microphoneLabel",microphone.label());
        try{startForegroundService(intent);accessCode.setText("");microphoneRunning=true;refreshState();status.setText("Starting microphone sharing...");bindMicrophone();}catch(RuntimeException error){microphoneRunning=false;microphoneRequested=false;refreshState();status.setText("Cannot start microphone sharing: "+error.getMessage());}
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request==41){if(grants.length>0&&grants[0]==android.content.pm.PackageManager.PERMISSION_GRANTED)shareCamera();else {cameraRequested=false;status.setText("Camera permission was denied");refreshState();}}else if(request==42)shareCamera();else if(request==43){if(grants.length>0&&grants[0]==android.content.pm.PackageManager.PERMISSION_GRANTED)shareMicrophone();else {microphoneRequested=false;status.setText("Microphone permission was denied");refreshState();}}else if(request==44)shareMicrophone();}
    @Override protected void onStart(){super.onStart();visible=true;bindCamera();bindMicrophone();}
    private void fit(){int w=videoArea.getWidth(),h=videoArea.getHeight();if(w==0 || h==0)return;double scale=Math.min(w/(double)streamWidth,h/(double)streamHeight);FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)video.getLayoutParams();int nw=(int)(streamWidth*scale),nh=(int)(streamHeight*scale);if(p.width!=nw || p.height!=nh){p.width=nw;p.height=nh;p.gravity=Gravity.CENTER;video.setLayoutParams(p);}}
    private void reveal(){if(session==null)return;input.enable(false);toolbar.setVisibility(View.VISIBLE);handler.removeCallbacks(hideToolbar);if(!paused)handler.postDelayed(hideToolbar,5000);}
    private void settings(){paused=true;input.enable(false);handler.removeCallbacks(hideToolbar);new AlertDialog.Builder(this).setTitle("Client settings").setMessage(capabilities.getText()+"\n\nStylus: contact, hover, pressure, tilt, primary barrel button and eraser require support from the device and host. Barrel rotation and secondary stylus buttons are not forwarded. A passive capacitive stylus is touch input.\n\nUSB/Bluetooth keyboards use the host keyboard layout. IME composition and system-reserved keys are not forwarded.\n\nStatistics show received data and submitted decoder output, not end-to-end latency.")
        .setPositiveButton(showStats?"Hide statistics":"Show statistics",(d,w)->{showStats=!showStats;stats.setVisibility(showStats?View.VISIBLE:View.GONE);paused=false;reveal();})
        .setNegativeButton("Resume",(d,w)->{paused=false;reveal();}).setOnCancelListener(d->{paused=false;reveal();}).show();}
    private void disconnect(){input.enable(false);ClientSession s=session;if(s!=null){status.setText("Disconnecting…");s.cancel();}}
    private void back(){if(session!=null){showResources();}else finish();}
    @SuppressWarnings("deprecation")
    @android.annotation.SuppressLint("GestureBackNavigation") // API 30-32 fallback; 33+ uses OnBackInvokedDispatcher above.
    @Override public void onBackPressed(){back();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(session!=null){int code=event.getKeyCode();if(code==KeyEvent.KEYCODE_F10 || code==KeyEvent.KEYCODE_F8 || (code==KeyEvent.KEYCODE_Q && event.isCtrlPressed() && event.isShiftPressed())){if(event.getAction()==KeyEvent.ACTION_DOWN){input.reset();if(code==KeyEvent.KEYCODE_Q)disconnectAll();else{paused=true;reveal();}}return true;}if(input.key(event))return true;}return super.dispatchKeyEvent(event);
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus)input.enable(false);else if(session!=null)input.enable(!paused && toolbar.getVisibility()!=View.VISIBLE);}
    @Override protected void onStop(){visible=false;disconnect();if(cameraBinder!=null){cameraBinder.listen(null);cameraBinder=null;}if(cameraBound){unbindService(cameraConnection);cameraBound=false;}if(microphoneBinder!=null){microphoneBinder.listen(null);microphoneBinder=null;}if(microphoneBound){unbindService(microphoneConnection);microphoneBound=false;}super.onStop();}
    @Override protected void onDestroy(){disconnect();getSystemService(android.hardware.input.InputManager.class).unregisterInputDeviceListener(this);handler.removeCallbacksAndMessages(null);super.onDestroy();}
    public void surfaceCreated(SurfaceHolder holder){surfaceReady=true;}
    public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    public void surfaceDestroyed(SurfaceHolder holder){surfaceReady=false;disconnect();}
}
