package io.github.eekebin.extrapane;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Point;
import android.os.Bundle;
import android.os.Handler;
import android.view.*;
import android.widget.*;
import java.io.File;
import java.net.*;
import java.util.*;

/** Explicit foreground offers. Publishing metadata never starts capture. */
public final class PeerActivity extends Activity implements SurfaceHolder.Callback {
    private final Handler handler=new Handler();
    private final Map<String,Offered> offered=new LinkedHashMap<>();
    private final Map<String,Runnable> cancellations=new HashMap<>();
    private final Map<String,View> activeRows=new HashMap<>();
    private final InputQueue inputQueue=new InputQueue();
    private final RemoteInput input=new RemoteInput(inputQueue);
    private PeerService peers;
    private PeerBeacon beacon;
    private LinearLayout page,offerRows,connections,videoArea,trustedRows;
    private TextView status,code;
    private Spinner interfaces;
    private EditText port;
    private Button sharing;
    private CheckBox pairing,inputAllowed;
    private SurfaceView surface;
    private Button fullscreenButton;
    private boolean fullscreen;
    private int streamWidth=16,streamHeight=9;
    private boolean listening,foreground,surfaceReady,videoActive,updating;
    private String displayKey;
    private String[] pendingDisplay,displayEvent;
    private final ArrayList<String> addresses=new ArrayList<>();
    private record Offered(String id,String label,int kind,Object source,CheckBox selected) {}
    private String key(String[] event){return event[1]+":"+event[3];}
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView label(String text,int size){TextView view=new TextView(this);view.setText(text);view.setTextSize(size);view.setTextColor(Color.rgb(231,239,249));view.setPadding(0,dp(10),0,dp(8));return view;}
    private Button button(String text,Runnable action){Button view=new Button(this);view.setText(text);view.setAllCaps(false);view.setOnClickListener(v->action.run());return view;}
    private void status(String text){status.setText(text);}
    private interface Action {void run()throws Exception;}
    private void safely(Action action){try{action.run();}catch(Exception e){status(e.getMessage()==null?"Device sharing failed":e.getMessage());}}
    @Override public void onCreate(Bundle saved){super.onCreate(saved);
        ScrollView scroll=new ScrollView(this);page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);page.setPadding(dp(20),dp(20),dp(20),dp(32));page.setBackgroundColor(Color.rgb(14,21,33));scroll.addView(page);setContentView(scroll);
        page.addView(label("ExtraPane · Devices",26));page.addView(label("Choose what this phone offers. Another paired device can request those resources while this page is open. No camera or microphone starts just from being listed.",14));
        interfaces=new Spinner(this);page.addView(interfaces);port=new EditText(this);port.setHint("Listening port");port.setInputType(2);port.setText("43825");page.addView(port);
        pairing=new CheckBox(this);pairing.setText("Require pairing code");pairing.setChecked(true);pairing.setTextColor(Color.WHITE);page.addView(pairing);
        code=label("",22);page.addView(code);sharing=button("Start sharing",this::toggleSharing);page.addView(sharing);
        status=label("Choose resources below",14);page.addView(status);offerRows=new LinearLayout(this);offerRows.setOrientation(LinearLayout.VERTICAL);page.addView(offerRows);
        inputAllowed=new CheckBox(this);inputAllowed.setText("Send touch, pen, mouse and keyboard input from this display");inputAllowed.setTextColor(Color.WHITE);inputAllowed.setChecked(true);page.addView(inputAllowed);
        page.addView(label("Active resources",20));connections=new LinearLayout(this);connections.setOrientation(LinearLayout.VERTICAL);page.addView(connections);
        page.addView(label("Paired devices",20));trustedRows=new LinearLayout(this);trustedRows.setOrientation(LinearLayout.VERTICAL);page.addView(trustedRows);
        videoArea=new LinearLayout(this);videoArea.setOrientation(LinearLayout.VERTICAL);videoArea.setGravity(Gravity.CENTER_HORIZONTAL);page.addView(videoArea);fullscreenButton=button("Fullscreen",()->setFullscreen(!fullscreen));videoArea.addView(fullscreenButton);surface=new SurfaceView(this);surface.getHolder().addCallback(this);surface.setFocusableInTouchMode(true);surface.setOnTouchListener((view,event)->input.motion(view,event));surface.setOnGenericMotionListener((view,event)->input.motion(view,event));videoArea.addView(surface,new LinearLayout.LayoutParams(-1,dp(300)));videoArea.setVisibility(View.GONE);
        page.addView(label("Android can offer its display, cameras, microphones and speakers. Creating system-wide virtual cameras/microphones, USB export, and sharing the phone's existing screen are not available here yet.",14));
        page.addView(button("Back to direct connections",this::finish));
        safely(()->{peers=new PeerService(new File(getNoBackupFilesDir(),"peer-trust"),android.os.Build.MODEL);code.setText("Pairing code: "+peers.pairingCode());refreshInterfaces();refreshOffers();refreshTrusted();});
        pairing.setOnCheckedChangeListener((v,checked)->{if(peers!=null)safely(()->peers.setPairingRequired(checked));code.setVisibility(checked?View.VISIBLE:View.GONE);});
        inputAllowed.setOnCheckedChangeListener((v,checked)->publish());
    }
    private void refreshInterfaces(){addresses.clear();try{for(NetworkInterface nic:Collections.list(NetworkInterface.getNetworkInterfaces()))if(nic.isUp()&&!nic.isLoopback())for(InetAddress address:Collections.list(nic.getInetAddresses()))if(address instanceof Inet4Address&&!address.isLoopbackAddress()&&!address.isLinkLocalAddress())addresses.add(address.getHostAddress());}catch(Exception e){status("Network addresses could not be read");}if(addresses.isEmpty())addresses.add("127.0.0.1");interfaces.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,addresses));}
    private void refreshTrusted(){if(peers==null)return;safely(()->{trustedRows.removeAllViews();for(String[] device:peers.remembered())if(device.length>=2){String id=device[0];trustedRows.addView(button(device[1]+" · "+id.substring(0,Math.min(8,id.length()))+" · Forget",()->new android.app.AlertDialog.Builder(this).setTitle("Forget this device?").setMessage("Its active connections will close. A new pairing code will be needed to reconnect.").setPositiveButton("Forget",(dialog,which)->safely(()->peers.forget(id))).setNegativeButton("Cancel",null).show()));}if(trustedRows.getChildCount()==0)trustedRows.addView(label("No paired devices",14));});}
    private void setFullscreen(boolean value){fullscreen=value;for(int i=0;i<page.getChildCount();++i){View child=page.getChildAt(i);if(child!=videoArea)child.setVisibility(value?View.GONE:View.VISIBLE);}if(!value)code.setVisibility(pairing.isChecked()?View.VISIBLE:View.GONE);fullscreenButton.setText(value?"Show controls":"Fullscreen");fitVideo();}
    private void fitVideo(){Point size=new Point();getDisplay().getRealSize(size);int width=Math.max(dp(120),page.getWidth()-page.getPaddingLeft()-page.getPaddingRight());int height=Math.max(1,width*streamHeight/Math.max(1,streamWidth));int maximum=fullscreen?size.y-dp(110):dp(340);if(height>maximum){height=maximum;width=Math.max(1,height*streamWidth/Math.max(1,streamHeight));}surface.setLayoutParams(new LinearLayout.LayoutParams(width,height));}
    private String opaque(String local){android.content.SharedPreferences settings=getSharedPreferences("peer-offer-identifiers",MODE_PRIVATE);String id=settings.getString(local,null);if(id==null){id=UUID.randomUUID().toString();settings.edit().putString(local,id).apply();}return id;}
    private void addOffer(String local,String label,int kind,Object source){String id=opaque(local);CheckBox selected=new CheckBox(this);selected.setText(label);selected.setTextColor(Color.WHITE);offerRows.addView(selected);Offered item=new Offered(id,label,kind,source,selected);offered.put(id,item);selected.setOnCheckedChangeListener((view,checked)->{if(updating)return;String permission=kind==1?Manifest.permission.CAMERA:kind==2?Manifest.permission.RECORD_AUDIO:null;if(checked&&permission!=null&&checkSelfPermission(permission)!=PackageManager.PERMISSION_GRANTED){updating=true;selected.setChecked(false);updating=false;requestPermissions(new String[]{permission},kind);status("Allow permission, then select this resource again");return;}publish();});}
    private void refreshOffers(){offerRows.removeAllViews();offered.clear();addOffer("display","Use this phone as another display",0,null);try{for(CameraCapture.Choice choice:CameraCapture.choices(this))addOffer("camera:"+choice.id(),choice.toString(),1,choice);}catch(Exception e){offerRows.addView(label("Camera inventory unavailable: "+e.getMessage(),14));}for(SpeakerSession.Choice choice:SpeakerSession.choices(this))addOffer("speaker:"+choice.id(),choice.label(),4,choice);for(MicrophoneCapture.Choice choice:MicrophoneCapture.choices(this))addOffer("microphone:"+choice.id(),choice.label(),2,choice);publish();}
    private void publish(){if(peers==null)return;safely(()->{ArrayList<String[]> rows=new ArrayList<>();Point size=new Point();getDisplay().getRealSize(size);int hz=Math.max(1,Math.min(1000,Math.round(getDisplay().getRefreshRate())));for(Offered item:offered.values())if(item.selected().isChecked()){int width=item.kind()==0?size.x:0,height=item.kind()==0?size.y:0;rows.add(new String[]{item.id(),item.label(),""+item.kind(),"true",item.kind()==0&&inputAllowed.isChecked()?"true":"false",""+width,""+height,item.kind()==0?""+hz:"0",""});}peers.offers(rows.toArray(new String[0][]));});}
    private void toggleSharing(){if(peers==null)return;safely(()->{if(listening){stopSharing();return;}int number=Integer.parseInt(port.getText().toString());if(number<1||number>65535)throw new IllegalArgumentException("Enter a port from 1 to 65535");publish();peers.start(addresses.get(interfaces.getSelectedItemPosition()),number);listening=true;sharing.setEnabled(false);status("Starting sharing…");});}
    private void stopSharing(){if(fullscreen)setFullscreen(false);if(beacon!=null){beacon.close();beacon=null;}if(peers!=null)safely(()->peers.stop());for(Runnable cancel:new ArrayList<>(cancellations.values()))cancel.run();cancellations.clear();activeRows.clear();connections.removeAllViews();pendingDisplay=null;displayEvent=null;videoActive=false;displayKey=null;input.enable(false);videoArea.setVisibility(View.GONE);listening=false;sharing.setText("Start sharing");sharing.setEnabled(true);interfaces.setEnabled(true);port.setEnabled(true);status("Sharing stopped");}
    private final Runnable poll=new Runnable(){public void run(){if(!foreground||peers==null)return;safely(()->{for(String[] event:peers.poll())event(event);});handler.postDelayed(this,30);}};
    private void event(String[] event)throws Exception{if(event.length<15)return;switch(event[0]){
        case "listening" -> {listening=true;sharing.setEnabled(true);sharing.setText("Stop sharing");interfaces.setEnabled(false);port.setEnabled(false);status("Listening on "+interfaces.getSelectedItem()+":"+event[6]);try{if(beacon!=null)beacon.close();beacon=new PeerBeacon(peers,interfaces.getSelectedItem().toString(),Integer.parseInt(event[6]));}catch(Exception e){status("Listening on "+interfaces.getSelectedItem()+":"+event[6]+". LAN discovery unavailable; connect by address.");}}
        case "native-ready" -> {try{startNative(event);}catch(Exception e){ended(event,e.getMessage()==null?"Resource could not start":e.getMessage());}}
        case "native-stop" -> stopNative(key(event));
        case "error" -> {sharing.setEnabled(true);status(event[7]);}
        case "disconnected" -> status("Device disconnected");
        case "trust-changed", "connected" -> refreshTrusted();
        default -> {}
    }}
    private void stopNative(String key){Runnable cancel=cancellations.remove(key);if(cancel!=null)cancel.run();View row=activeRows.remove(key);if(row!=null)connections.removeView(row);if(key.equals(displayKey)){if(fullscreen)setFullscreen(false);pendingDisplay=null;displayEvent=null;videoActive=false;displayKey=null;input.enable(false);videoArea.setVisibility(View.GONE);}}
    private void ended(String[] event,String reason){handler.post(()->{String key=key(event);if(!activeRows.containsKey(key)&&!key.equals(displayKey))return;stopNative(key);status(reason);if(peers!=null)safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));});}
    private void startNative(String[] event)throws Exception{String key=key(event);Offered item=offered.get(event[9]);if(!foreground||!listening||item==null||!item.selected().isChecked()){peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3]));return;}if(cancellations.containsKey(key))return;
        Button stop=button(item.label()+" · Disconnect",()->{stopNative(key);safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));});connections.addView(stop);activeRows.put(key,stop);
        int port=Integer.parseInt(event[6]);String credential=event[13];if(item.kind()!=0)event[13]="";
        if(item.kind()==0){if(displayKey!=null){peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3]));stopNative(key);return;}displayKey=key;pendingDisplay=event.clone();displayEvent=event.clone();videoArea.setVisibility(View.VISIBLE);if(surfaceReady)startDisplay();return;}
        if(item.kind()==1){if(checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){ended(event,"Camera permission was withdrawn");return;}CameraSession session=new CameraSession(this,"127.0.0.1",port,"",null,credential,(CameraCapture.Choice)item.source(),new CameraSession.Listener(){public void status(String text){}public void connected(){handler.post(()->status("Camera connected"));}public void ended(String reason){PeerActivity.this.ended(event,reason);}});cancellations.put(key,session::cancel);session.start();}
        else if(item.kind()==2){if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){ended(event,"Microphone permission was withdrawn");return;}MicrophoneSession session=new MicrophoneSession(this,"127.0.0.1",port,"",null,credential,(MicrophoneCapture.Choice)item.source(),new MicrophoneSession.Listener(){public void status(String text){}public void connected(){handler.post(()->status("Microphone connected"));}public void ended(String reason){PeerActivity.this.ended(event,reason);}});cancellations.put(key,session::cancel);session.start();}
        else if(item.kind()==4){SpeakerSession session=new SpeakerSession(this,"127.0.0.1",port,"",null,credential,(SpeakerSession.Choice)item.source(),new SpeakerSession.Listener(){public void status(String text){}public void connected(){handler.post(()->status("Speakers connected"));}public void ended(String reason){PeerActivity.this.ended(event,reason);}});cancellations.put(key,session::cancel);session.start();}
    }
    private void startDisplay(){if(pendingDisplay==null||!surfaceReady)return;String[] event=pendingDisplay;pendingDisplay=null;String credential=event[13];event[13]="";if(displayEvent!=null)displayEvent[13]="";Point size=new Point();getDisplay().getRealSize(size);int w=Integer.parseInt(event[10]),h=Integer.parseInt(event[11]),hz=Integer.parseInt(event[12]);if(w==0){w=size.x;h=size.y;hz=Math.max(1,Math.min(360,Math.round(getDisplay().getRefreshRate())));}final boolean controls=event[14].equals("true")||event[14].equals("1");
        ClientSession session=new ClientSession("127.0.0.1","",null,surface.getHolder().getSurface(),w,h,hz,controls?15:0,0,inputQueue,new ClientSession.Listener(){public void status(String message){}public void connected(){handler.post(()->{videoActive=true;input.enable(controls);surface.requestFocus();status("Display connected");});}public void format(int width,int height,int caps,String decoder){handler.post(()->{streamWidth=width;streamHeight=height;fitVideo();});}public void statistics(String text){}public void ended(String reason){PeerActivity.this.ended(event,reason);}},Integer.parseInt(event[6]),credential,true);cancellations.put(key(event),session::cancel);session.start();
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event){if(videoActive&&event.getKeyCode()!=KeyEvent.KEYCODE_BACK&&input.key(event))return true;return super.dispatchKeyEvent(event);}
    public void surfaceCreated(SurfaceHolder holder){surfaceReady=true;startDisplay();}public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}public void surfaceDestroyed(SurfaceHolder holder){surfaceReady=false;if(displayKey!=null){String[] event=displayEvent;stopNative(displayKey);if(event!=null&&peers!=null)safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));}}
    @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(!focused)input.reset();}
    @Override protected void onResume(){super.onResume();foreground=true;if(peers!=null)safely(()->peers.setForeground(true));handler.post(poll);}
    @Override protected void onPause(){foreground=false;handler.removeCallbacks(poll);if(peers!=null)safely(()->peers.setForeground(false));stopSharing();super.onPause();}
    @Override protected void onStop(){foreground=false;handler.removeCallbacks(poll);stopSharing();super.onStop();}
    @Override protected void onDestroy(){handler.removeCallbacks(poll);if(peers!=null){PeerService closing=peers;peers=null;new Thread(closing::close,"ExtraPane peer shutdown").start();}super.onDestroy();}
}
