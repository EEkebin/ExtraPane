package io.github.eekebin.extrapane;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.view.*;
import android.view.animation.DecelerateInterpolator;
import android.widget.*;
import java.io.File;
import java.net.*;
import java.util.*;

/** Explicit foreground offers. Publishing metadata never starts capture. */
public final class PeerActivity extends Activity implements SurfaceHolder.Callback {
    private static final int BG=0xff0e1521, PANEL=0xff172336, TEXT=0xffe7eff9,
            MUTED=0xffa3b4ca, ACCENT=0xff8abdff, BORDER=0xff35465e, RED=0xffa93643, GREEN=0xff237a49;
    private final Handler handler=new Handler();
    private final Map<String,Offered> offered=new LinkedHashMap<>();
    private final Map<String,Runnable> cancellations=new HashMap<>();
    private final Map<String,View> activeRows=new HashMap<>();
    private final Map<Integer,ResourceGroup> offerGroups=new LinkedHashMap<>(), activeGroups=new LinkedHashMap<>();
    private final Map<String,LinearLayout> pages=new LinkedHashMap<>();
    private final Map<String,Button> navigation=new LinkedHashMap<>();
    private final InputQueue inputQueue=new InputQueue();
    private final RemoteInput input=new RemoteInput(inputQueue);
    private PeerService peers;
    private PeerBeacon beacon;
    private final SharingLifecycle sharingLifecycle=new SharingLifecycle();
    private String sharingAddress="";
    private LinearLayout page,offerRows,connections,videoArea,trustedRows,toolbar,drawer;
    private FrameLayout root,drawerLayer;
    private ScrollView scroll;
    private TextView status,code,pageTitle,connectionEmpty;
    private ImageButton menuButton;
    private Spinner interfaces;
    private EditText port;
    private Button sharing,refreshResources;
    private CheckBox pairing,inputAllowed;
    private SurfaceView surface;
    private Button fullscreenButton;
    private boolean fullscreen,drawerOpen;
    private String selectedPage="Devices";
    private int streamWidth=16,streamHeight=9;
    private boolean foreground,surfaceReady,videoActive,updating;
    private String displayKey;
    private String[] pendingDisplay,displayEvent;
    private final ArrayList<String> addresses=new ArrayList<>();
    private record Offered(String id,String label,int kind,Object source,PowerButton selected) {}
    private String key(String[] event){return event[1]+":"+event[3];}
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private GradientDrawable rounded(int color,int stroke){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(14));if(stroke!=0)d.setStroke(dp(1),stroke);return d;}
    private LinearLayout column(){LinearLayout layout=new LinearLayout(this);layout.setOrientation(LinearLayout.VERTICAL);return layout;}
    private LinearLayout.LayoutParams full(){return new LinearLayout.LayoutParams(-1,-2);}
    private TextView label(String text,int size){TextView view=new TextView(this);view.setText(text);view.setTextSize(size);view.setTextColor(TEXT);view.setPadding(0,dp(8),0,dp(8));view.setLineSpacing(dp(2),1);return view;}
    private TextView note(String text){TextView v=label(text,14);v.setTextColor(MUTED);return v;}
    private Button button(String text,Runnable action){Button v=new Button(this);v.setText(text);v.setTextSize(14);v.setAllCaps(false);v.setTextColor(TEXT);v.setMinHeight(dp(48));v.setMinimumHeight(dp(48));v.setPadding(dp(16),dp(10),dp(16),dp(10));v.setBackground(rounded(PANEL,BORDER));LinearLayout.LayoutParams lp=full();lp.topMargin=dp(8);lp.bottomMargin=dp(8);v.setLayoutParams(lp);v.setOnClickListener(w->action.run());return v;}
    private CheckBox check(String text){CheckBox v=new CheckBox(this);v.setText(text);v.setTextSize(15);v.setTextColor(TEXT);v.setMinHeight(dp(52));v.setPadding(dp(4),dp(8),dp(4),dp(8));v.setButtonTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{ACCENT,MUTED}));return v;}
    /** Resource actions use power icons; checked means offered, not actively streaming. */
    private final class PowerButton extends ImageButton implements Checkable {
        private final String resourceLabel;
        private boolean checked;
        private java.util.function.Consumer<Boolean> changed;
        PowerButton(String resourceLabel){
            super(PeerActivity.this);this.resourceLabel=resourceLabel;
            setImageDrawable(new LineIcon("power"));setPadding(dp(12),dp(12),dp(12),dp(12));
            setMinimumWidth(dp(48));setMinimumHeight(dp(48));setScaleType(ImageView.ScaleType.CENTER);
            setOnClickListener(view->toggle());render();
        }
        private void render(){
            setSelected(checked);setBackground(new android.graphics.drawable.RippleDrawable(
                    ColorStateList.valueOf(0x33ffffff),rounded(checked?RED:GREEN,0),rounded(Color.WHITE,0)));
            setContentDescription((checked?"Disable sharing of ":"Enable sharing of ")+resourceLabel);
            setTooltipText((checked?"Disable sharing of ":"Enable sharing of ")+resourceLabel);
            setStateDescription(checked?"Available for sharing":"Not shared");
        }
        @Override public boolean isChecked(){return checked;}
        @Override public void setChecked(boolean value){if(checked==value)return;checked=value;render();if(changed!=null)changed.accept(value);}
        @Override public void toggle(){setChecked(!checked);}
        void setOnCheckedChangeListener(java.util.function.Consumer<Boolean> listener){changed=listener;}
        @Override public void onInitializeAccessibilityNodeInfo(android.view.accessibility.AccessibilityNodeInfo info){super.onInitializeAccessibilityNodeInfo(info);info.setCheckable(true);info.setChecked(checked);}
    }
    private LinearLayout resourceRow(String name,PowerButton power){
        LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER_VERTICAL);row.setPadding(0,dp(4),0,dp(4));
        TextView title=label(name,16);title.setPadding(dp(4),dp(8),dp(12),dp(8));row.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        row.addView(power,new LinearLayout.LayoutParams(dp(48),dp(48)));return row;
    }
    private void status(String text){status.setText(text);status.setVisibility(fullscreen||text==null||text.isBlank()?View.GONE:View.VISIBLE);}
    private interface Action {void run()throws Exception;}
    private void safely(Action action){try{action.run();}catch(Exception e){status(e.getMessage()==null?"Device sharing failed":e.getMessage());}}

    /** Small native line icons, independent of font glyph coverage. */
    private final class LineIcon extends Drawable {
        private final String type;private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        LineIcon(String type){this.type=type;setBounds(0,0,dp(24),dp(24));}
        @Override public void draw(Canvas canvas){canvas.save();canvas.translate(getBounds().left,getBounds().top);canvas.scale(getBounds().width()/24f,getBounds().height()/24f);paint.setColor(ACCENT);paint.setStrokeWidth(1.8f);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);paint.setStyle(Paint.Style.STROKE);
            switch(type){
                case "power" -> {paint.setColor(TEXT);canvas.drawArc(4,5,20,21,-50,280,false,paint);canvas.drawLine(12,3,12,12,paint);}
                case "menu" -> {canvas.drawLine(4,6,20,6,paint);canvas.drawLine(4,12,20,12,paint);canvas.drawLine(4,18,20,18,paint);}
                case "chevron" -> {canvas.drawLine(9,5,16,12,paint);canvas.drawLine(16,12,9,19,paint);}
                case "Monitors", "Devices" -> {canvas.drawRoundRect(3,4,21,16,2,2,paint);canvas.drawLine(12,16,12,21,paint);canvas.drawLine(8,21,16,21,paint);}
                case "Cameras" -> {canvas.drawRoundRect(3,6,21,20,2,2,paint);canvas.drawCircle(12,13,4,paint);canvas.drawLine(8,6,9,3,paint);canvas.drawLine(9,3,15,3,paint);canvas.drawLine(15,3,16,6,paint);}
                case "Speakers" -> {canvas.drawRect(3,9,7,15,paint);canvas.drawLine(7,9,12,5,paint);canvas.drawLine(12,5,12,19,paint);canvas.drawLine(12,19,7,15,paint);canvas.drawArc(10,5,21,19,-55,110,false,paint);canvas.drawArc(12,8,18,16,-55,110,false,paint);}
                case "Microphones" -> {canvas.drawRoundRect(9,3,15,15,3,3,paint);canvas.drawArc(5,8,19,19,0,180,false,paint);canvas.drawLine(12,19,12,22,paint);}
                case "Share" -> {canvas.drawLine(12,16,12,3,paint);canvas.drawLine(7,8,12,3,paint);canvas.drawLine(12,3,17,8,paint);canvas.drawLine(4,13,4,21,paint);canvas.drawLine(4,21,20,21,paint);canvas.drawLine(20,21,20,13,paint);}
                case "Connections", "USB" -> {canvas.drawRoundRect(3,8,12,16,4,4,paint);canvas.drawRoundRect(12,8,21,16,4,4,paint);canvas.drawLine(8,12,16,12,paint);}
                default -> {canvas.drawCircle(12,12,9,paint);canvas.drawLine(12,11,12,17,paint);canvas.drawPoint(12,7,paint);}
            }canvas.restore();}
        @Override public void setAlpha(int a){paint.setAlpha(a);}
        @Override public void setColorFilter(ColorFilter filter){paint.setColorFilter(filter);}
        @Override public int getOpacity(){return PixelFormat.TRANSLUCENT;}
        @Override public int getIntrinsicWidth(){return dp(24);}
        @Override public int getIntrinsicHeight(){return dp(24);}
    }
    private final class ResourceGroup {
        final LinearLayout box=column(),body=column();final TextView title;final ImageView arrow;final String name;
        boolean expanded=true;
        ResourceGroup(String name,LinearLayout parent){this.name=name;box.setBackground(rounded(PANEL,BORDER));LinearLayout.LayoutParams params=full();params.topMargin=dp(8);params.bottomMargin=dp(8);parent.addView(box,params);
            LinearLayout header=new LinearLayout(PeerActivity.this);header.setGravity(Gravity.CENTER_VERTICAL);header.setPadding(dp(14),dp(8),dp(14),dp(8));header.setMinimumHeight(dp(56));header.setFocusable(true);header.setClickable(true);
            ImageView icon=new ImageView(PeerActivity.this);icon.setImageDrawable(new LineIcon(name));header.addView(icon,new LinearLayout.LayoutParams(dp(24),dp(24)));
            title=label(name,17);title.setTypeface(null,Typeface.BOLD);title.setPadding(dp(12),0,dp(12),0);title.setGravity(Gravity.CENTER);header.addView(title,new LinearLayout.LayoutParams(0,-2,1));
            arrow=new ImageView(PeerActivity.this);arrow.setImageDrawable(new LineIcon("chevron"));arrow.setRotation(90);header.addView(arrow,new LinearLayout.LayoutParams(dp(24),dp(24)));box.addView(header,full());body.setPadding(dp(12),0,dp(12),dp(12));box.addView(body,full());
            header.setContentDescription(name+", expanded");header.setOnClickListener(v->{expanded=!expanded;body.setVisibility(expanded?View.VISIBLE:View.GONE);arrow.animate().rotation(expanded?90:0).setDuration(160).start();header.setContentDescription(name+(expanded?", expanded":", collapsed"));});
        }
        void count(int count){title.setText(name+"  ("+count+")");}
    }
    @Override public void onCreate(Bundle saved){super.onCreate(saved);
        getWindow().setDecorFitsSystemWindows(false);getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);if(android.os.Build.VERSION.SDK_INT>=33)getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,this::back);
        root=new FrameLayout(this);root.setBackgroundColor(BG);setContentView(root);
        root.setOnApplyWindowInsetsListener((view,insets)->{android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());android.graphics.Insets ime=insets.getInsets(WindowInsets.Type.ime());view.setPadding(bars.left,bars.top,bars.right,Math.max(bars.bottom,ime.bottom));return insets;});root.requestApplyInsets();
        LinearLayout shell=column();root.addView(shell,new FrameLayout.LayoutParams(-1,-1));
        toolbar=new LinearLayout(this);toolbar.setGravity(Gravity.CENTER_VERTICAL);toolbar.setPadding(dp(8),dp(4),dp(16),dp(4));shell.addView(toolbar,new LinearLayout.LayoutParams(-1,dp(64)));
        menuButton=new ImageButton(this);menuButton.setImageDrawable(new LineIcon("menu"));menuButton.setContentDescription("Open navigation menu");menuButton.setBackground(rounded(BG,0));menuButton.setOnClickListener(v->showDrawer(!drawerOpen));toolbar.addView(menuButton,new LinearLayout.LayoutParams(dp(48),dp(48)));
        ImageView logo=new ImageView(this);logo.setImageResource(R.drawable.extrapane);logo.setContentDescription("ExtraPane");LinearLayout.LayoutParams logoParams=new LinearLayout.LayoutParams(dp(28),dp(28));logoParams.setMargins(dp(4),0,dp(12),0);toolbar.addView(logo,logoParams);pageTitle=label("Devices",21);pageTitle.setTypeface(null,Typeface.BOLD);toolbar.addView(pageTitle,new LinearLayout.LayoutParams(0,-2,1));
        scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);shell.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));page=column();page.setPadding(dp(16),dp(4),dp(20),dp(24));scroll.addView(page,new ScrollView.LayoutParams(-1,-2));
        status=note("");status.setVisibility(View.GONE);page.addView(status,full());
        // Keep the same SurfaceView attached across navigation; hiding a page must not end a stream.
        videoArea=column();videoArea.setGravity(Gravity.CENTER_HORIZONTAL);page.addView(videoArea,full());fullscreenButton=button("Fullscreen",()->setFullscreen(!fullscreen));videoArea.addView(fullscreenButton);surface=new SurfaceView(this);surface.getHolder().addCallback(this);surface.setFocusableInTouchMode(true);surface.setOnTouchListener((view,event)->{if(event.getActionMasked()==android.view.MotionEvent.ACTION_DOWN)view.requestFocus();return input.motion(view,event);});surface.setOnGenericMotionListener((view,event)->input.motion(view,event));videoArea.addView(surface,new LinearLayout.LayoutParams(-1,dp(300)));videoArea.setVisibility(View.GONE);
        for(String name:new String[]{"Devices","Share","Connections","About"}){LinearLayout section=column();pages.put(name,section);page.addView(section,full());}
        buildDevices();buildShare();buildConnections();buildAbout();buildDrawer();selectPage("Devices");
        safely(()->{peers=new PeerService(new File(getNoBackupFilesDir(),"peer-trust"),android.os.Build.MODEL);code.setText(peers.pairingCode());refreshInterfaces();refreshOffers();refreshTrusted();});
        pairing.setOnCheckedChangeListener((v,checked)->{if(peers!=null)safely(()->peers.setPairingRequired(checked));code.setVisibility(checked?View.VISIBLE:View.GONE);});inputAllowed.setOnCheckedChangeListener((v,checked)->publish());
    }
    private void buildDevices(){LinearLayout content=pages.get("Devices");content.addView(label("Your devices, together",25));content.addView(note("Offer this phone's resources from Share, then select this phone in ExtraPane on your computer. Keep ExtraPane open while sharing."));content.addView(button("Choose resources to share",()->selectPage("Share")));content.addView(label("Paired devices",19));trustedRows=column();content.addView(trustedRows,full());}
    private void buildShare(){LinearLayout content=pages.get("Share");content.addView(label("Share from this phone",25));content.addView(note("Select what other devices may use. A camera or microphone starts only when a connected device requests it."));
        LinearLayout setup=column();setup.setPadding(dp(16),dp(8),dp(16),dp(12));setup.setBackground(rounded(PANEL,BORDER));content.addView(setup,full());setup.addView(note("Network address"));interfaces=new Spinner(this);interfaces.setMinimumHeight(dp(48));interfaces.setBackgroundTintList(ColorStateList.valueOf(ACCENT));interfaces.setPopupBackgroundDrawable(rounded(PANEL,BORDER));setup.addView(interfaces,full());setup.addView(note("Listening port"));port=new EditText(this);port.setSingleLine(true);port.setInputType(2);port.setText("43825");port.setTextColor(TEXT);port.setTextSize(16);port.setPadding(dp(12),dp(12),dp(12),dp(12));port.setBackground(rounded(BG,BORDER));port.setMinimumHeight(dp(48));setup.addView(port,full());
        pairing=check("Require pairing code");pairing.setChecked(true);setup.addView(pairing,full());code=label("",28);code.setLetterSpacing(.15f);code.setTypeface(Typeface.MONOSPACE,Typeface.BOLD);code.setTextIsSelectable(true);setup.addView(code,full());sharing=button("Stop sharing all",this::stopSharing);sharing.setBackground(rounded(RED,0));sharing.setVisibility(View.GONE);setup.addView(sharing);
        refreshResources=button("Refresh available resources",()->{if(activeRows.isEmpty())safely(this::refreshOffers);});content.addView(refreshResources);offerRows=column();content.addView(offerRows,full());inputAllowed=check("Allow touch, pen, mouse and keyboard input");inputAllowed.setChecked(true);
    }
    private void buildConnections(){connections=pages.get("Connections");connections.addView(label("Connections",25));connectionEmpty=note("No resources are connected. Choose resources in Share, then connect from your other device.");connections.addView(connectionEmpty,full());for(int kind:new int[]{0,1,4,2,3}){ResourceGroup group=new ResourceGroup(groupName(kind),connections);group.box.setVisibility(View.GONE);activeGroups.put(kind,group);}}
    private void buildAbout(){LinearLayout content=pages.get("About");ImageView logo=new ImageView(this);logo.setImageResource(R.drawable.extrapane);content.addView(logo,new LinearLayout.LayoutParams(dp(72),dp(72)));content.addView(label("ExtraPane",26));content.addView(note("Share your devices across your network."));content.addView(note("Version "+BuildConfig.VERSION_NAME));content.addView(note("Android offers its display, cameras, microphones and speakers while this app stays open. System-wide virtual camera/microphone devices, USB sharing and capture of this phone's existing screen are not available on Android."));content.addView(button("GitHub repository",()->startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW,android.net.Uri.parse("https://github.com/EEkebin/ExtraPane")))));content.addView(button("Legacy connections",()->{startActivity(new android.content.Intent(this,MainActivity.class));finish();}));}
    private void buildDrawer(){drawerLayer=new FrameLayout(this);drawerLayer.setVisibility(View.GONE);root.addView(drawerLayer,new FrameLayout.LayoutParams(-1,-1));View scrim=new View(this);scrim.setBackgroundColor(0x99000000);scrim.setContentDescription("Close navigation menu");scrim.setOnClickListener(v->showDrawer(false));drawerLayer.addView(scrim,new FrameLayout.LayoutParams(-1,-1));drawer=column();drawer.setPadding(dp(16),dp(16),dp(16),dp(16));drawer.setBackgroundColor(BG);drawer.setClickable(true);FrameLayout.LayoutParams drawerParams=new FrameLayout.LayoutParams(Math.min(dp(300),getResources().getDisplayMetrics().widthPixels-dp(56)),-1,Gravity.START);drawerLayer.addView(drawer,drawerParams);ScrollView drawerScroll=new ScrollView(this);drawerScroll.setFillViewport(true);drawer.addView(drawerScroll,new LinearLayout.LayoutParams(-1,-1));LinearLayout drawerContent=column();drawerScroll.addView(drawerContent,new ScrollView.LayoutParams(-1,-2));
        LinearLayout brand=new LinearLayout(this);brand.setGravity(Gravity.CENTER_VERTICAL);ImageView icon=new ImageView(this);icon.setImageResource(R.drawable.extrapane);brand.addView(icon,new LinearLayout.LayoutParams(dp(32),dp(32)));TextView name=label("ExtraPane",23);name.setPadding(dp(12),dp(16),0,dp(20));name.setTypeface(null,Typeface.BOLD);brand.addView(name);drawerContent.addView(brand,full());
        for(String nameKey:new String[]{"Devices","Share","Connections","About"}){Button nav=button(nameKey,()->{selectPage(nameKey);showDrawer(false);});nav.setGravity(Gravity.CENTER_VERTICAL|Gravity.START);nav.setCompoundDrawables(new LineIcon(nameKey),null,null,null);nav.setCompoundDrawablePadding(dp(16));drawerContent.addView(nav);navigation.put(nameKey,nav);}
    }
    private void selectPage(String name){input.reset();surface.clearFocus();page.setFocusableInTouchMode(true);page.requestFocus();selectedPage=name;pageTitle.setText(name);for(Map.Entry<String,LinearLayout> entry:pages.entrySet())entry.getValue().setVisibility(!fullscreen&&entry.getKey().equals(name)?View.VISIBLE:View.GONE);for(Map.Entry<String,Button> entry:navigation.entrySet())entry.getValue().setBackground(rounded(entry.getKey().equals(name)?0xff223c5b:BG,0));if(!videoActive)scroll.smoothScrollTo(0,0);}
    private void showDrawer(boolean open){drawerOpen=open;drawer.animate().cancel();drawerLayer.animate().cancel();if(open){input.reset();surface.clearFocus();page.requestFocus();drawerLayer.setVisibility(View.VISIBLE);drawer.setTranslationX(-drawer.getLayoutParams().width);drawer.animate().translationX(0).setInterpolator(new DecelerateInterpolator()).setDuration(180).start();menuButton.setContentDescription("Close navigation menu");}else{drawer.animate().translationX(-drawer.getLayoutParams().width).setDuration(160).withEndAction(()->{if(!drawerOpen)drawerLayer.setVisibility(View.GONE);}).start();menuButton.setContentDescription("Open navigation menu");}}
    private String groupName(int kind){return switch(kind){case 0,5->"Monitors";case 1->"Cameras";case 2->"Microphones";case 3->"USB";case 4->"Speakers";default->"Resources";};}
    private void refreshInterfaces(){addresses.clear();try{for(NetworkInterface nic:Collections.list(NetworkInterface.getNetworkInterfaces()))if(nic.isUp()&&!nic.isLoopback())for(InetAddress address:Collections.list(nic.getInetAddresses()))if(address instanceof Inet4Address&&!address.isLoopbackAddress()&&!address.isLinkLocalAddress())addresses.add(address.getHostAddress());}catch(Exception e){status("Network addresses could not be read");}if(addresses.isEmpty())addresses.add("127.0.0.1");ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,addresses){@Override public View getView(int position,View recycled,ViewGroup parent){TextView v=(TextView)super.getView(position,recycled,parent);v.setTextColor(TEXT);v.setTextSize(16);v.setPadding(dp(12),dp(10),dp(12),dp(10));return v;}@Override public View getDropDownView(int position,View recycled,ViewGroup parent){TextView v=(TextView)super.getDropDownView(position,recycled,parent);v.setTextColor(TEXT);v.setBackgroundColor(PANEL);v.setMinHeight(dp(48));return v;}};interfaces.setAdapter(adapter);}
    private void refreshTrusted(){if(peers==null)return;safely(()->{trustedRows.removeAllViews();for(String[] device:peers.remembered())if(device.length>=2){String id=device[0];LinearLayout row=column();row.setPadding(dp(14),dp(8),dp(14),dp(8));row.setBackground(rounded(PANEL,BORDER));LinearLayout.LayoutParams params=full();params.bottomMargin=dp(12);trustedRows.addView(row,params);row.addView(label(device[1],17));row.addView(button("Forget device",()->new android.app.AlertDialog.Builder(this).setTitle("Forget this device?").setMessage("Its active connections will close. A new pairing code will be needed to reconnect.").setPositiveButton("Forget",(dialog,which)->safely(()->peers.forget(id))).setNegativeButton("Cancel",null).show()));}if(trustedRows.getChildCount()==0)trustedRows.addView(note("No paired devices yet. Enable a resource to pair your first device."));});}
    private void setFullscreen(boolean value){fullscreen=value;if(drawerOpen)showDrawer(false);toolbar.setVisibility(value?View.GONE:View.VISIBLE);status.setVisibility(value||status.getText().length()==0?View.GONE:View.VISIBLE);selectPage(selectedPage);fullscreenButton.setText(value?"Exit fullscreen":"Fullscreen");fitVideo();}
    private void fitVideo(){int width=Math.max(dp(120),page.getWidth()-page.getPaddingLeft()-page.getPaddingRight());int height=Math.max(1,width*streamHeight/Math.max(1,streamWidth));int maximum=fullscreen?Math.max(dp(120),scroll.getHeight()-dp(88)):dp(340);if(height>maximum){height=maximum;width=Math.max(1,height*streamWidth/Math.max(1,streamHeight));}surface.setLayoutParams(new LinearLayout.LayoutParams(width,height));}
    @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);FrameLayout.LayoutParams params=(FrameLayout.LayoutParams)drawer.getLayoutParams();params.width=Math.min(dp(300),getResources().getDisplayMetrics().widthPixels-dp(56));drawer.setLayoutParams(params);page.post(this::fitVideo);}
    @android.annotation.SuppressLint("GestureBackNavigation") // API 30-32 fallback; 33+ uses OnBackInvokedDispatcher.
    @Override public void onBackPressed(){back();}
    private void back(){if(drawerOpen){showDrawer(false);return;}if(fullscreen){setFullscreen(false);return;}finish();}
    private String opaque(String local){android.content.SharedPreferences settings=getSharedPreferences("peer-offer-identifiers",MODE_PRIVATE);String id=settings.getString(local,null);if(id==null){id=UUID.randomUUID().toString();settings.edit().putString(local,id).apply();}return id;}
    private void addOffer(String local,String label,int kind,Object source){String id=opaque(local);PowerButton selected=new PowerButton(label);ResourceGroup group=offerGroups.get(kind);group.body.addView(resourceRow(label,selected),full());Offered item=new Offered(id,label,kind,source,selected);offered.put(id,item);selected.setOnCheckedChangeListener(checked->{if(updating)return;String permission=kind==1?Manifest.permission.CAMERA:kind==2?Manifest.permission.RECORD_AUDIO:null;if(checked&&permission!=null&&checkSelfPermission(permission)!=PackageManager.PERMISSION_GRANTED){updating=true;selected.setChecked(false);updating=false;requestPermissions(new String[]{permission},kind);status("Allow permission, then select this resource again");return;}changeOffer();});}
    private void refreshOffers(){Set<String> selectedIds=new HashSet<>();for(Offered old:offered.values())if(old.selected().isChecked())selectedIds.add(old.id());if(inputAllowed.getParent() instanceof ViewGroup oldParent)oldParent.removeView(inputAllowed);offerRows.removeAllViews();offerGroups.clear();offered.clear();for(int kind:new int[]{0,1,4,2,3})offerGroups.put(kind,new ResourceGroup(groupName(kind),offerRows));addOffer("display","This phone's display",0,null);Point size=new Point();getDisplay().getRealSize(size);offerGroups.get(0).body.addView(note(size.x+" x "+size.y+" / "+Math.round(getDisplay().getRefreshRate())+" Hz. Shows a virtual monitor created on your other computer."));offerGroups.get(0).body.addView(inputAllowed,full());
        try{for(CameraCapture.Choice choice:CameraCapture.choices(this))addOffer("camera:"+choice.id(),choice.toString(),1,choice);}catch(Exception e){offerGroups.get(1).body.addView(note("Camera inventory unavailable: "+e.getMessage()));}
        for(SpeakerSession.Choice choice:SpeakerSession.choices(this))addOffer("speaker:"+choice.id(),choice.label(),4,choice);
        for(MicrophoneCapture.Choice choice:MicrophoneCapture.choices(this))addOffer("microphone:"+choice.id(),choice.label(),2,choice);
        for(Map.Entry<Integer,ResourceGroup> entry:offerGroups.entrySet()){int count=0;for(Offered item:offered.values())if(item.kind()==entry.getKey())++count;entry.getValue().count(count);if(count==0)entry.getValue().body.addView(note(entry.getKey()==3?"USB sharing is not available on Android.":"No available devices."));}updating=true;try{for(Offered item:offered.values())item.selected().setChecked(selectedIds.contains(item.id()));}finally{updating=false;}publish();}
    private void updateConnectionGroups(){int total=0;for(ResourceGroup group:activeGroups.values()){int count=group.body.getChildCount();group.count(count);group.box.setVisibility(count>0?View.VISIBLE:View.GONE);total+=count;}connectionEmpty.setVisibility(total==0?View.VISIBLE:View.GONE);refreshResources.setEnabled(total==0);refreshResources.setAlpha(total==0?1f:.45f);}
    private boolean hasOffers(){for(Offered item:offered.values())if(item.selected().isChecked())return true;return false;}
    private void updateSharingControls(){boolean active=sharingLifecycle.active();sharing.setVisibility(active||hasOffers()?View.VISIBLE:View.GONE);sharing.setEnabled(true);interfaces.setEnabled(!active);port.setEnabled(!active);}
    private void publishSelected()throws java.io.IOException{
        ArrayList<String[]> rows=new ArrayList<>();Point size=new Point();getDisplay().getRealSize(size);int hz=Math.max(1,Math.min(1000,Math.round(getDisplay().getRefreshRate())));
        for(Offered item:offered.values())if(item.selected().isChecked()){int width=item.kind()==0?size.x:0,height=item.kind()==0?size.y:0;rows.add(new String[]{item.id(),item.label(),""+item.kind(),"true",item.kind()==0&&inputAllowed.isChecked()?"true":"false",""+width,""+height,item.kind()==0?""+hz:"0",""});}
        peers.offers(rows.toArray(new String[0][]));
    }
    private void publish(){if(peers==null)return;try{publishSelected();if(!hasOffers()&&sharingLifecycle.active())stopSharing();}catch(Exception e){stopSharing();status(e.getMessage()==null?"Device sharing failed":e.getMessage());}updateSharingControls();}
    private void changeOffer(){
        try{
            if(peers==null||!foreground)throw new IllegalStateException("Keep ExtraPane open to share a resource");
            if(!hasOffers()){stopSharing();return;}
            if(!sharingLifecycle.active()){
                int number;try{number=Integer.parseInt(port.getText().toString().trim());}catch(NumberFormatException e){throw new IllegalArgumentException("Enter a port from 1 to 65535");}
                if(number<1||number>65535)throw new IllegalArgumentException("Enter a port from 1 to 65535");
                int index=interfaces.getSelectedItemPosition();if(index<0||index>=addresses.size())throw new IllegalArgumentException("Select a local network address");
                String address=addresses.get(index);InetAddress local=InetAddress.getByName(address);NetworkInterface nic=NetworkInterface.getByInetAddress(local);
                if(nic==null||!nic.isUp())throw new IllegalArgumentException("The selected network address is no longer available");
                publishSelected();sharingAddress=address;sharingLifecycle.begin(peers.start(address,number));status("Starting sharing...");
            }else publishSelected();
        }catch(Exception e){
            // A failed update must never leave an enabled-looking resource or stale catalog.
            stopSharing();status(e.getMessage()==null?"Device sharing failed":e.getMessage());
        }
        updateSharingControls();
    }
    private void stopSharing(){
        sharingLifecycle.stop();updating=true;try{for(Offered item:offered.values())item.selected().setChecked(false);}finally{updating=false;}
        if(fullscreen)setFullscreen(false);if(beacon!=null){beacon.close();beacon=null;}
        if(peers!=null){safely(()->peers.offers(new String[0][]));safely(()->peers.stop());}
        for(Runnable cancel:new ArrayList<>(cancellations.values()))cancel.run();cancellations.clear();activeRows.clear();for(ResourceGroup group:activeGroups.values())group.body.removeAllViews();updateConnectionGroups();pendingDisplay=null;displayEvent=null;videoActive=false;displayKey=null;input.enable(false);videoArea.setVisibility(View.GONE);updateSharingControls();status("Sharing stopped");
    }
    private final Runnable poll=new Runnable(){public void run(){if(!foreground||peers==null)return;safely(()->{for(String[] event:peers.poll())event(event);});handler.postDelayed(this,30);}};
    private void event(String[] event)throws Exception{if(event.length<15)return;switch(event[0]){
        case "listening" -> {if(!foreground||!hasOffers()||event.length<=18||!sharingLifecycle.confirm(Long.parseLong(event[18])))return;updateSharingControls();status("Listening on "+sharingAddress+":"+event[6]);try{if(beacon!=null)beacon.close();beacon=new PeerBeacon(peers,sharingAddress,Integer.parseInt(event[6]));}catch(Exception e){status("Listening on "+sharingAddress+":"+event[6]+". LAN discovery unavailable; connect by address.");}}
        case "native-ready" -> {try{startNative(event);}catch(Exception e){ended(event,e.getMessage()==null?"Resource could not start":e.getMessage());}}
        case "native-stop" -> stopNative(key(event));
        case "error" -> {long generation=event.length>18?Long.parseLong(event[18]):0;if(generation!=0){if(!sharingLifecycle.matches(generation))return;stopSharing();}status(event[7]);}
        case "disconnected" -> status("Device disconnected");
        case "trust-changed", "connected" -> refreshTrusted();
        default -> {}
    }}
    private void stopNative(String key){Runnable cancel=cancellations.remove(key);if(cancel!=null)cancel.run();View row=activeRows.remove(key);if(row!=null&&row.getParent() instanceof ViewGroup parent)parent.removeView(row);updateConnectionGroups();if(key.equals(displayKey)){if(fullscreen)setFullscreen(false);pendingDisplay=null;displayEvent=null;videoActive=false;displayKey=null;input.enable(false);videoArea.setVisibility(View.GONE);}}
    private void ended(String[] event,String reason){handler.post(()->{String key=key(event);if(!activeRows.containsKey(key)&&!key.equals(displayKey))return;stopNative(key);status(reason);if(peers!=null)safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));});}
    private void startNative(String[] event)throws Exception{String key=key(event);Offered item=offered.get(event[9]);if(!foreground||!sharingLifecycle.listening()||item==null||!item.selected().isChecked()){peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3]));return;}if(cancellations.containsKey(key))return;
        PowerButton stop=new PowerButton(item.label());stop.setChecked(true);stop.setContentDescription("Disconnect "+item.label());stop.setTooltipText("Disconnect "+item.label());stop.setStateDescription("Connected");stop.setOnClickListener(view->{stopNative(key);safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));});LinearLayout row=resourceRow(item.label(),stop);activeGroups.get(item.kind()).body.addView(row,full());activeRows.put(key,row);updateConnectionGroups();
        int port=Integer.parseInt(event[6]);String credential=event[13];if(item.kind()!=0)event[13]="";
        if(item.kind()==0){if(displayKey!=null){peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3]));stopNative(key);return;}displayKey=key;pendingDisplay=event.clone();displayEvent=event.clone();videoArea.setVisibility(View.VISIBLE);if(surfaceReady)startDisplay();return;}
        if(item.kind()==1){if(checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){ended(event,"Camera permission was withdrawn");return;}CameraSession session=new CameraSession(this,"127.0.0.1",port,"",null,credential,(CameraCapture.Choice)item.source(),new CameraSession.Listener(){public void status(String text){}public void connected(){handler.post(()->{if(activeRows.containsKey(key))status("Camera connected");});}public void ended(String reason){PeerActivity.this.ended(event,reason);}});cancellations.put(key,session::cancel);session.start();}
        else if(item.kind()==2){if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){ended(event,"Microphone permission was withdrawn");return;}MicrophoneSession session=new MicrophoneSession(this,"127.0.0.1",port,"",null,credential,(MicrophoneCapture.Choice)item.source(),new MicrophoneSession.Listener(){public void status(String text){}public void connected(){handler.post(()->{if(activeRows.containsKey(key))status("Microphone connected");});}public void ended(String reason){PeerActivity.this.ended(event,reason);}});cancellations.put(key,session::cancel);session.start();}
        else if(item.kind()==4){SpeakerSession session=new SpeakerSession(this,"127.0.0.1",port,"",null,credential,(SpeakerSession.Choice)item.source(),new SpeakerSession.Listener(){public void status(String text){}public void connected(){handler.post(()->{if(activeRows.containsKey(key))status("Speakers connected");});}public void ended(String reason){PeerActivity.this.ended(event,reason);}});cancellations.put(key,session::cancel);session.start();}
    }
    private void startDisplay(){if(pendingDisplay==null||!surfaceReady)return;String[] event=pendingDisplay;pendingDisplay=null;String credential=event[13];event[13]="";if(displayEvent!=null)displayEvent[13]="";Point size=new Point();getDisplay().getRealSize(size);int w=Integer.parseInt(event[10]),h=Integer.parseInt(event[11]),hz=Integer.parseInt(event[12]);if(w==0){w=size.x;h=size.y;hz=Math.max(1,Math.min(360,Math.round(getDisplay().getRefreshRate())));}final boolean controls=event[14].equals("true")||event[14].equals("1");
        ClientSession session=new ClientSession("127.0.0.1","",null,surface.getHolder().getSurface(),w,h,hz,controls?15:0,0,inputQueue,new ClientSession.Listener(){public void status(String message){}public void connected(){handler.post(()->{if(!key(event).equals(displayKey)||!foreground||!sharingLifecycle.listening())return;videoActive=true;input.enable(controls);surface.requestFocus();status("Display connected");});}public void format(int width,int height,int caps,String decoder){handler.post(()->{streamWidth=width;streamHeight=height;fitVideo();});}public void statistics(String text){}public void ended(String reason){PeerActivity.this.ended(event,reason);}},Integer.parseInt(event[6]),credential,true);cancellations.put(key(event),session::cancel);session.start();
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event){if(videoActive&&!drawerOpen&&surface.hasFocus()&&event.getKeyCode()!=KeyEvent.KEYCODE_BACK&&input.key(event))return true;return super.dispatchKeyEvent(event);}
    public void surfaceCreated(SurfaceHolder holder){surfaceReady=true;startDisplay();}public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}public void surfaceDestroyed(SurfaceHolder holder){surfaceReady=false;if(displayKey!=null){String[] event=displayEvent;stopNative(displayKey);if(event!=null&&peers!=null)safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));}}
    @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(!focused)input.reset();}
    @Override protected void onResume(){super.onResume();foreground=true;if(peers!=null)safely(()->{peers.setForeground(true);if(!sharingLifecycle.listening()&&activeRows.isEmpty())refreshOffers();});handler.post(poll);}
    @Override protected void onPause(){foreground=false;handler.removeCallbacks(poll);if(peers!=null)safely(()->peers.setForeground(false));stopSharing();super.onPause();}
    @Override protected void onStop(){foreground=false;handler.removeCallbacks(poll);stopSharing();super.onStop();}
    @Override protected void onDestroy(){handler.removeCallbacks(poll);if(peers!=null){PeerService closing=peers;peers=null;new Thread(closing::close,"ExtraPane peer shutdown").start();}super.onDestroy();}
}
