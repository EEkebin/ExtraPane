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
    private final ProviderSessions providerSessions=new ProviderSessions();
    private long nextListenAttempt;
    private boolean listenFailed;
    private List<String> failedNetworks=List.of();
    private final Map<Integer,ResourceGroup> offerGroups=new LinkedHashMap<>(), activeGroups=new LinkedHashMap<>();
    private final Map<String,LinearLayout> pages=new LinkedHashMap<>();
    private final Map<String,Button> navigation=new LinkedHashMap<>();
    private final Map<String,Boolean> deviceExpanded=new HashMap<>();
    private final InputQueue inputQueue=new InputQueue();
    private final RemoteInput input=new RemoteInput(inputQueue);
    private PeerService peers;
    private PeerBeacon beacon;
    private NearbyPeerSearch nearbySearch,knownPeerObserver;
    private long observerEpoch;
    private NearbyPeers nearby=new NearbyPeers();
    private long searchEpoch;
    private final Map<Long,String> connectedPeers=new HashMap<>(),pendingPeers=new HashMap<>();
    private final Map<Long,String> peerEndpoints=new HashMap<>();
    private LinearLayout nearbyRows,unpairedSection,unpairedRows,endpointSection,endpointRows;
    private final Map<Long,Map<String,String>> endpointsVia=new LinkedHashMap<>();
    private ImageButton searchDevices;
    private LinearLayout searchDots;
    private final List<android.animation.Animator> searchAnimations=new ArrayList<>();
    private TextView searchStatus;
    private Runnable updateConnectDialog;
    private android.app.AlertDialog pairingDialog;
    private final SharingLifecycle sharingLifecycle=new SharingLifecycle();
    private String sharingAddress="";
    private LinearLayout page,offerRows,connections,videoArea,trustedRows,toolbar,drawer;
    private FrameLayout root,drawerLayer;
    private ScrollView scroll;
    private TextView status,code,pageTitle,connectionEmpty;
    private ImageButton menuButton;
    private Spinner interfaces;
    private EditText port;
    private Button sharing,refreshResources,applyNetwork;
    private CheckBox pairing;
    private PowerButton inputAllowed;
    private SurfaceView surface;
    private Button fullscreenButton;
    private boolean fullscreen,drawerOpen;
    private final DrawerSwipe drawerSwipe=new DrawerSwipe();
    private boolean drawerSwipeConsumed,drawerDragStartedOpen,drawerDragCancelled;
    private float drawerProgress;
    private View drawerScrim;
    private VelocityTracker drawerVelocity;
    private android.animation.ValueAnimator drawerAnimator;
    private String selectedPage="Devices";
    private final PageHistory pageHistory=new PageHistory();
    private int streamWidth=16,streamHeight=9;
    private boolean foreground,surfaceReady,videoActive;
    private String displayKey;
    private String[] pendingDisplay,displayEvent;
    private final ArrayList<String> addresses=new ArrayList<>();
    private record Offered(String id,String label,int kind,Object source,PowerButton selected,TextView availability) {}
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
                case "refresh" -> {canvas.drawArc(4,4,20,20,-55,290,false,paint);canvas.drawLine(20,3,20,9,paint);canvas.drawLine(14,9,20,9,paint);}
                case "menu" -> {canvas.drawLine(4,6,20,6,paint);canvas.drawLine(4,12,20,12,paint);canvas.drawLine(4,18,20,18,paint);}
                case "chevron" -> {canvas.drawLine(9,5,16,12,paint);canvas.drawLine(16,12,9,19,paint);}
                case "Monitors", "Devices" -> {canvas.drawRoundRect(3,4,21,16,2,2,paint);canvas.drawLine(12,16,12,21,paint);canvas.drawLine(8,21,16,21,paint);}
                case "Cameras" -> {canvas.drawRoundRect(3,6,21,20,2,2,paint);canvas.drawCircle(12,13,4,paint);canvas.drawLine(8,6,9,3,paint);canvas.drawLine(9,3,15,3,paint);canvas.drawLine(15,3,16,6,paint);}
                case "Speakers" -> {canvas.drawRect(3,9,7,15,paint);canvas.drawLine(7,9,12,5,paint);canvas.drawLine(12,5,12,19,paint);canvas.drawLine(12,19,7,15,paint);canvas.drawArc(10,5,21,19,-55,110,false,paint);canvas.drawArc(12,8,18,16,-55,110,false,paint);}
                case "Microphones" -> {canvas.drawRoundRect(9,3,15,15,3,3,paint);canvas.drawArc(5,8,19,19,0,180,false,paint);canvas.drawLine(12,19,12,22,paint);}
                case "Share" -> {canvas.drawLine(12,16,12,3,paint);canvas.drawLine(7,8,12,3,paint);canvas.drawLine(12,3,17,8,paint);canvas.drawLine(4,13,4,21,paint);canvas.drawLine(4,21,20,21,paint);canvas.drawLine(20,21,20,13,paint);}
                case "Resources", "USB" -> {canvas.drawRoundRect(3,8,12,16,4,4,paint);canvas.drawRoundRect(12,8,21,16,4,4,paint);canvas.drawLine(8,12,16,12,paint);}
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
            arrow=new ImageView(PeerActivity.this);arrow.setImageDrawable(new LineIcon("chevron"));arrow.setRotation(90);header.addView(arrow,new LinearLayout.LayoutParams(dp(24),dp(24)));box.addView(header,full());View divider=new View(PeerActivity.this);divider.setBackgroundColor(BORDER);box.addView(divider,new LinearLayout.LayoutParams(-1,dp(1)));body.setPadding(dp(12),dp(8),dp(12),dp(12));box.addView(body,full());
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
        for(String name:new String[]{"Devices","Resources","About"}){LinearLayout section=column();pages.put(name,section);page.addView(section,full());}
        buildDevices();buildResources();buildSharingSettings();buildAbout();buildDrawer();selectPage("Devices");
        safely(()->{peers=new PeerService(new File(getNoBackupFilesDir(),"peer-trust"),android.os.Build.MODEL);code.setText(peers.pairingCode());refreshInterfaces();refreshOffers();refreshTrusted();});
        pairing.setOnCheckedChangeListener((v,checked)->{if(peers!=null)safely(()->peers.setPairingRequired(checked));code.setVisibility(checked?View.VISIBLE:View.GONE);});inputAllowed.setOnCheckedChangeListener(checked->{inputAllowed.setContentDescription(checked?"Block input":"Allow input");inputAllowed.setTooltipText(checked?"Block input":"Allow input");inputAllowed.setStateDescription(checked?"Input allowed":"Input blocked");input.enable(checked&&videoActive&&displayEvent!=null&&(displayEvent[14].equals("true")||displayEvent[14].equals("1")));publish();});
    }
    private boolean containsPoint(View view,float x,float y){android.graphics.Rect bounds=new android.graphics.Rect();return view.getGlobalVisibleRect(bounds)&&bounds.contains((int)x,(int)y);}
    private boolean interactiveAt(View view,float x,float y){
        if(!containsPoint(view,x,y))return false;
        if(view instanceof SurfaceView||view instanceof EditText||view instanceof Spinner||view.isClickable()||view.isLongClickable()||(view instanceof TextView text&&text.isTextSelectable()))return true;
        if(view instanceof ViewGroup group)for(int i=group.getChildCount()-1;i>=0;--i)if(interactiveAt(group.getChildAt(i),x,y))return true;
        return false;
    }
    @Override public boolean dispatchTouchEvent(MotionEvent event){
        int action=event.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN){
            drawerSwipeConsumed=false;drawerDragCancelled=false;drawerDragStartedOpen=drawerOpen;
            boolean eligible=drawer!=null&&!fullscreen&&(drawerOpen||(scroll!=null&&containsPoint(scroll,event.getRawX(),event.getRawY())&&!interactiveAt(page,event.getRawX(),event.getRawY())));
            drawerSwipe.begin(event.getRawX(),event.getRawY(),eligible,drawerProgress,drawer==null?1:drawer.getLayoutParams().width);
            if(drawerVelocity!=null)drawerVelocity.recycle();drawerVelocity=eligible?VelocityTracker.obtain():null;
        }
        if(drawerVelocity!=null)drawerVelocity.addMovement(event);
        if(action==MotionEvent.ACTION_MOVE&&drawerSwipe.move(event.getRawX(),event.getRawY(),event.getPointerCount(),ViewConfiguration.get(this).getScaledTouchSlop())){
            if(!drawerSwipeConsumed){
                MotionEvent cancel=MotionEvent.obtain(event);cancel.setAction(MotionEvent.ACTION_CANCEL);super.dispatchTouchEvent(cancel);cancel.recycle();
                cancelDrawerAnimation();drawerSwipeConsumed=true;input.reset();surface.clearFocus();page.requestFocus();
            }
            setDrawerProgress(drawerSwipe.progress());return true;
        }
        if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL||action==MotionEvent.ACTION_POINTER_DOWN){
            boolean consumed=drawerSwipeConsumed;
            if(consumed){
                float velocity=0;if(drawerVelocity!=null&&action==MotionEvent.ACTION_UP){drawerVelocity.computeCurrentVelocity(1000,ViewConfiguration.get(this).getScaledMaximumFlingVelocity());velocity=drawerVelocity.getXVelocity();}
                boolean open=action==MotionEvent.ACTION_UP&&!drawerDragCancelled?drawerSwipe.release(velocity,dp(400)):drawerDragStartedOpen;
                drawerSwipe.cancel();if(action==MotionEvent.ACTION_POINTER_DOWN)drawerDragCancelled=true;showDrawer(open);
            }else drawerSwipe.cancel();
            if(drawerVelocity!=null){drawerVelocity.recycle();drawerVelocity=null;}
            if(action!=MotionEvent.ACTION_POINTER_DOWN)drawerSwipeConsumed=false;
            if(consumed)return true;
        }
        if(drawerSwipeConsumed)return true;
        return super.dispatchTouchEvent(event);
    }
    private void sectionHeading(LinearLayout parent,String text){TextView heading=label(text,25);heading.setTypeface(null,Typeface.BOLD);heading.setPadding(0,dp(24),0,dp(12));parent.addView(heading,full());View line=new View(this);line.setBackgroundColor(BORDER);LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,dp(1));params.bottomMargin=dp(16);parent.addView(line,params);}
    private void searchVisual(boolean searching){for(android.animation.Animator animation:searchAnimations)animation.cancel();searchAnimations.clear();searchDevices.setRotation(0);searchDevices.setEnabled(!searching);searchDevices.setContentDescription(searching?"Searching nearby devices":"Search nearby devices");searchDots.setVisibility(searching?View.VISIBLE:View.GONE);for(int i=0;i<3;++i)searchDots.getChildAt(i).setTranslationY(0);if(!searching||!android.animation.ValueAnimator.areAnimatorsEnabled())return;
        android.animation.ObjectAnimator spin=android.animation.ObjectAnimator.ofFloat(searchDevices,"rotation",0,360);spin.setDuration(1000);spin.setRepeatCount(android.animation.ValueAnimator.INFINITE);spin.setInterpolator(new android.view.animation.LinearInterpolator());searchAnimations.add(spin);spin.start();
        for(int i=0;i<3;++i){android.animation.ObjectAnimator bounce=android.animation.ObjectAnimator.ofFloat(searchDots.getChildAt(i),"translationY",0,-dp(5),0);bounce.setDuration(700);bounce.setStartDelay(i*140L);bounce.setRepeatCount(android.animation.ValueAnimator.INFINITE);searchAnimations.add(bounce);bounce.start();}
    }
    private void buildDevices(){LinearLayout content=pages.get("Devices");content.addView(label("Devices",25));content.addView(note("Pair once to remember your other devices. Pairing does not start a resource; use its power button in Resources when you need it."));
        sectionHeading(content,"This device");LinearLayout setup=column();setup.setPadding(dp(16),dp(8),dp(16),dp(12));setup.setBackground(rounded(PANEL,BORDER));content.addView(setup,full());setup.addView(note("Network address"));interfaces=new Spinner(this);interfaces.setMinimumHeight(dp(48));interfaces.setBackgroundTintList(ColorStateList.valueOf(ACCENT));interfaces.setPopupBackgroundDrawable(rounded(PANEL,BORDER));setup.addView(interfaces,full());setup.addView(note("Listening port"));port=new EditText(this);port.setSingleLine(true);port.setInputType(2);port.setText("43825");port.setTextColor(TEXT);port.setTextSize(16);port.setPadding(dp(12),dp(12),dp(12),dp(12));port.setBackground(rounded(BG,BORDER));port.setMinimumHeight(dp(48));setup.addView(port,full());applyNetwork=button("Apply network settings",this::applyNetworkSettings);setup.addView(applyNetwork);
        pairing=check("Require pairing code");pairing.setChecked(true);setup.addView(pairing,full());code=label("",28);code.setLetterSpacing(.15f);code.setTypeface(Typeface.MONOSPACE,Typeface.BOLD);code.setTextIsSelectable(true);setup.addView(code,full());
        content.addView(button("Pair by address",()->connectDialog("","","",false)));
        sectionHeading(content,"Paired devices");trustedRows=column();content.addView(trustedRows,full());
        unpairedSection=column();sectionHeading(unpairedSection,"Unpaired devices");unpairedRows=column();unpairedSection.addView(unpairedRows,full());unpairedSection.setVisibility(View.GONE);content.addView(unpairedSection,full());
        endpointSection=column();sectionHeading(endpointSection,"Devices through paired devices");endpointRows=column();endpointSection.addView(endpointRows,full());endpointSection.setVisibility(View.GONE);content.addView(endpointSection,full());
        sectionHeading(content,"Nearby devices");LinearLayout searchRow=new LinearLayout(this);searchRow.setGravity(Gravity.CENTER_VERTICAL);searchDevices=new ImageButton(this);searchDevices.setImageDrawable(new LineIcon("refresh"));searchDevices.setBackground(rounded(PANEL,BORDER));searchDevices.setContentDescription("Search nearby devices");searchDevices.setTooltipText("Search nearby devices");searchDevices.setOnClickListener(view->searchNearby());searchRow.addView(searchDevices,new LinearLayout.LayoutParams(dp(48),dp(48)));searchStatus=note("Search for devices on this network.");searchRow.addView(searchStatus,new LinearLayout.LayoutParams(0,-2,1));searchDots=new LinearLayout(this);searchDots.setGravity(Gravity.CENTER);searchDots.setVisibility(View.GONE);searchDots.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);for(int i=0;i<3;++i){View dot=new View(this);dot.setBackground(rounded(ACCENT,0));LinearLayout.LayoutParams spacing=new LinearLayout.LayoutParams(dp(6),dp(6));spacing.setMargins(dp(3),dp(10),dp(3),dp(10));searchDots.addView(dot,spacing);}searchRow.addView(searchDots);content.addView(searchRow,full());nearbyRows=column();content.addView(nearbyRows,full());
    }
    private void buildResources(){connections=pages.get("Resources");connections.addView(label("Resources",25));connections.addView(note("Start a resource using its power button on the receiving device. Pairing alone does not start it. This phone shows available resources and lets you stop those in use."));
        sharing=button("Disconnect all",this::disconnectAll);sharing.setBackground(rounded(RED,0));sharing.setVisibility(View.GONE);connections.addView(sharing);
        refreshResources=button("Refresh available resources",()->safely(this::refreshOffers));offerRows=column();connections.addView(offerRows,full());inputAllowed=new PowerButton("Allow input");inputAllowed.setChecked(true);inputAllowed.setContentDescription("Block input");inputAllowed.setTooltipText("Block input");inputAllowed.setStateDescription("Input allowed");
        connections.addView(label("Active streams",19));connectionEmpty=note("No resources are connected. Request this phone's available resources from your other device.");connections.addView(connectionEmpty,full());for(int kind:new int[]{0,1,4,2,3}){ResourceGroup group=new ResourceGroup(groupName(kind),connections);group.box.setVisibility(View.GONE);activeGroups.put(kind,group);}
    }
    private void buildSharingSettings(){LinearLayout content=pages.get("Devices");content.addView(label("Sharing settings",25));content.addView(note("Supported resources are available while ExtraPane is open and their permissions are allowed. A paired device must request a resource before it starts."));content.addView(refreshResources);content.addView(note("View available resources, grant camera or microphone permissions, and disconnect active streams in Resources."));content.addView(button("Manage resources",()->selectPage("Resources")));}
    private void openProjectLink(String path){startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW,android.net.Uri.parse(path)));}
    private void buildAbout(){
        LinearLayout content=pages.get("About"),hero=column();hero.setPadding(dp(20),dp(20),dp(20),dp(20));hero.setBackground(rounded(PANEL,BORDER));content.addView(hero,full());
        LinearLayout brand=new LinearLayout(this);brand.setGravity(Gravity.CENTER_VERTICAL);LinearLayout introduction=column();TextView title=label("ExtraPane",30);title.setTypeface(null,Typeface.BOLD);introduction.addView(title);introduction.addView(note("Your devices, connected."));brand.addView(introduction,new LinearLayout.LayoutParams(0,-2,1));ImageView logo=new ImageView(this);logo.setImageResource(R.drawable.extrapane);logo.setScaleType(ImageView.ScaleType.FIT_CENTER);logo.setContentDescription("ExtraPane logo");brand.addView(logo,new LinearLayout.LayoutParams(dp(96),dp(96)));hero.addView(brand,full());
        TextView version=label("Running version: "+BuildConfig.VERSION_NAME,14);version.setTextColor(ACCENT);version.setTextIsSelectable(true);hero.addView(version,full());
        content.addView(note("Use your phone as another screen for your computer, or make its cameras, microphones and speakers available to a paired device over your network."));
        sectionHeading(content,"Made for your devices");content.addView(note("ExtraPane connects supported devices over Wi-Fi or Ethernet. Choose a reachable address directly, or search your local network. Pairing protects access; resources start only when requested."));content.addView(note("On Android, keep ExtraPane open while sharing. The phone receives a virtual monitor created on another computer; it does not create system-wide camera or microphone devices, share USB devices, or capture its existing screen."));
        sectionHeading(content,"Project");content.addView(button("Project website",()->openProjectLink("https://eekebin.github.io/ExtraPane/")));content.addView(button("GitHub repository",()->openProjectLink("https://github.com/EEkebin/ExtraPane")));content.addView(button("Documentation",()->openProjectLink("https://github.com/EEkebin/ExtraPane#readme")));content.addView(button("Releases and changelog",()->openProjectLink("https://github.com/EEkebin/ExtraPane/releases")));content.addView(button("Report an issue",()->openProjectLink("https://github.com/EEkebin/ExtraPane/issues")));
        content.addView(note("Third-party libraries retain their own licenses. A project-wide license has not been selected."));content.addView(button("Legacy interface",()->{startActivity(new android.content.Intent(this,MainActivity.class));finish();}));
    }
    private void buildDrawer(){drawerLayer=new FrameLayout(this);drawerLayer.setVisibility(View.GONE);root.addView(drawerLayer,new FrameLayout.LayoutParams(-1,-1));View scrim=new View(this);drawerScrim=scrim;scrim.setAlpha(0);scrim.setBackgroundColor(0x99000000);scrim.setContentDescription("Close navigation menu");scrim.setOnClickListener(v->showDrawer(false));drawerLayer.addView(scrim,new FrameLayout.LayoutParams(-1,-1));drawer=column();drawer.setPadding(dp(16),dp(16),dp(16),dp(16));drawer.setBackgroundColor(BG);drawer.setClickable(true);FrameLayout.LayoutParams drawerParams=new FrameLayout.LayoutParams(Math.min(dp(300),getResources().getDisplayMetrics().widthPixels-dp(56)),-1,Gravity.START);drawerLayer.addView(drawer,drawerParams);ScrollView drawerScroll=new ScrollView(this);drawerScroll.setFillViewport(true);drawer.addView(drawerScroll,new LinearLayout.LayoutParams(-1,-1));LinearLayout drawerContent=column();drawerScroll.addView(drawerContent,new ScrollView.LayoutParams(-1,-2));
        LinearLayout brand=new LinearLayout(this);brand.setGravity(Gravity.CENTER_VERTICAL);ImageView icon=new ImageView(this);icon.setImageResource(R.drawable.extrapane);brand.addView(icon,new LinearLayout.LayoutParams(dp(32),dp(32)));TextView name=label("ExtraPane",23);name.setPadding(dp(12),dp(16),0,dp(20));name.setTypeface(null,Typeface.BOLD);brand.addView(name);drawerContent.addView(brand,full());
        for(String nameKey:new String[]{"Devices","Resources","About"}){Button nav=button(nameKey,()->{selectPage(nameKey);showDrawer(false);});nav.setGravity(Gravity.CENTER_VERTICAL|Gravity.START);nav.setCompoundDrawables(new LineIcon(nameKey),null,null,null);nav.setCompoundDrawablePadding(dp(16));drawerContent.addView(nav);navigation.put(nameKey,nav);}
    }
    private void selectPage(String name){pageHistory.visit(name);renderPage(name);}
    private void renderPage(String name){input.reset();surface.clearFocus();page.setFocusableInTouchMode(true);page.requestFocus();selectedPage=name;pageTitle.setText(name);for(Map.Entry<String,LinearLayout> entry:pages.entrySet())entry.getValue().setVisibility(!fullscreen&&entry.getKey().equals(name)?View.VISIBLE:View.GONE);for(Map.Entry<String,Button> entry:navigation.entrySet())entry.getValue().setBackground(rounded(entry.getKey().equals(name)?0xff223c5b:BG,0));if(!videoActive)scroll.smoothScrollTo(0,0);}
    private void cancelDrawerAnimation(){if(drawerAnimator!=null){android.animation.ValueAnimator old=drawerAnimator;drawerAnimator=null;old.cancel();}}
    private void setDrawerProgress(float progress){
        drawerProgress=Math.max(0,Math.min(1,progress));drawer.setTranslationX((drawerProgress-1)*drawer.getLayoutParams().width);
        drawerScrim.setAlpha(drawerProgress);drawerLayer.setVisibility(drawerProgress>0||drawerSwipeConsumed?View.VISIBLE:View.GONE);
    }
    private void showDrawer(boolean open){
        cancelDrawerAnimation();drawerOpen=open;
        if(open){input.reset();surface.clearFocus();page.requestFocus();}
        menuButton.setContentDescription(open?"Close navigation menu":"Open navigation menu");
        android.animation.ValueAnimator animation=android.animation.ValueAnimator.ofFloat(drawerProgress,open?1f:0f);drawerAnimator=animation;
        animation.setDuration(Math.max(80,Math.round(220*Math.abs((open?1f:0f)-drawerProgress))));animation.setInterpolator(new DecelerateInterpolator());
        animation.addUpdateListener(value->setDrawerProgress((Float)value.getAnimatedValue()));
        animation.addListener(new android.animation.AnimatorListenerAdapter(){@Override public void onAnimationEnd(android.animation.Animator value){if(drawerAnimator==animation){drawerAnimator=null;setDrawerProgress(open?1:0);}}});animation.start();
    }
    private String groupName(int kind){return switch(kind){case 0,5->"Monitors";case 1->"Cameras";case 2->"Microphones";case 3->"USB";case 4->"Speakers";default->"Resources";};}
    private void refreshInterfaces(){List<String> before=new ArrayList<>(addresses);String previous=interfaces.getSelectedItem()==null?"":interfaces.getSelectedItem().toString();addresses.clear();try{for(NetworkInterface nic:Collections.list(NetworkInterface.getNetworkInterfaces()))if(nic.isUp()&&!nic.isLoopback())for(InetAddress address:Collections.list(nic.getInetAddresses()))if(address instanceof Inet4Address&&!address.isLoopbackAddress()&&!address.isLinkLocalAddress())addresses.add(address.getHostAddress());}catch(Exception e){status("Network addresses could not be read");}if(addresses.isEmpty())addresses.add("127.0.0.1");if(before.equals(addresses))return;ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,addresses){@Override public View getView(int position,View recycled,ViewGroup parent){TextView v=(TextView)super.getView(position,recycled,parent);v.setTextColor(TEXT);v.setTextSize(16);v.setPadding(dp(12),dp(10),dp(12),dp(10));return v;}@Override public View getDropDownView(int position,View recycled,ViewGroup parent){TextView v=(TextView)super.getDropDownView(position,recycled,parent);v.setTextColor(TEXT);v.setBackgroundColor(PANEL);v.setMinHeight(dp(48));return v;}};interfaces.setAdapter(adapter);int preserved=addresses.indexOf(previous);if(preserved>=0)interfaces.setSelection(preserved);}
    private EditText deviceField(String hint,String value,boolean numeric){EditText field=new EditText(this);field.setSingleLine(true);field.setHint(hint);field.setText(value);field.setTextColor(TEXT);field.setHintTextColor(MUTED);if(numeric)field.setInputType(2);return field;}
    private String endpoint(String host,String servicePort){
        String value=host.trim().toLowerCase(java.util.Locale.ROOT);if(value.equals("localhost"))value="127.0.0.1";
        String[] octets=value.split("\\.");if(octets.length==4)try{for(int i=0;i<4;++i){int n=Integer.parseInt(octets[i]);if(n<0||n>255)return "";octets[i]=""+n;}value=String.join(".",octets);}catch(NumberFormatException ignored){return "";}
        try{int number=Integer.parseInt(servicePort.trim());return number<1||number>65535?"":value+":"+number;}catch(NumberFormatException ignored){return "";}
    }
    private boolean connectedEndpoint(String address,String servicePort){String target=endpoint(address,servicePort);if(target.isEmpty())return false;for(long token:connectedPeers.keySet())if(target.equals(peerEndpoints.get(token)))return true;if(peers!=null)try{for(String[] remembered:peers.remembered())if(remembered.length>=4&&connectedPeers.containsValue(remembered[0])&&target.equals(endpoint(remembered[2],remembered[3])))return true;}catch(Exception ignored){}return false;}
    private void connectDialog(String expected,String address,String servicePort,boolean requireCode){
        if(connectedPeers.containsValue(expected)&&!expected.isEmpty())return;
        LinearLayout form=column();form.setPadding(dp(20),dp(8),dp(20),dp(8));form.setBackgroundColor(BG);
        EditText host=deviceField("IPv4 address",address,false),remotePort=deviceField("Port",servicePort.isBlank()?"43825":servicePort,true);form.addView(host,full());form.addView(remotePort,full());
        CheckBox open=check("Connect without pairing (identity will not be verified)");form.addView(open,full());if(requireCode)open.setVisibility(View.GONE);
        android.app.AlertDialog dialog=new android.app.AlertDialog.Builder(this).setTitle(expected.isEmpty()?"Pair by address":"Pair device").setView(form).setPositiveButton("Pair",null).setNegativeButton("Cancel",null).create();
        dialog.setOnShowListener(ignored->dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener(view->{
            try{
                int number=Integer.parseInt(remotePort.getText().toString().trim());if(number<1||number>65535)throw new IllegalArgumentException("Enter a port from 1 to 65535");
                String destination=host.getText().toString().trim();if(!destination.equalsIgnoreCase("localhost")&&!destination.matches("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}"))throw new IllegalArgumentException("Enter an IPv4 address");
                if(endpoint(destination,""+number).isEmpty())throw new IllegalArgumentException("Enter a valid IPv4 address");
                String local=interfaces.getSelectedItem()==null?"":interfaces.getSelectedItem().toString();if(destination.equalsIgnoreCase("localhost")||destination.startsWith("127."))local="127.0.0.1";
                if(connectedEndpoint(destination,""+number))return;dialog.dismiss();preparePairing(expected,destination,number,local,!requireCode&&open.isChecked());
            }catch(Exception error){status(error.getMessage()==null?"Device connection failed":error.getMessage());}
        }));dialog.show();
        TextView already=note("This device is already listed. Manage its resources in Resources.");form.addView(already);Runnable updateConnect=()->{boolean connected=connectedEndpoint(host.getText().toString(),remotePort.getText().toString());dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setVisibility(connected?View.GONE:View.VISIBLE);already.setVisibility(connected?View.VISIBLE:View.GONE);};android.text.TextWatcher changes=new android.text.TextWatcher(){public void beforeTextChanged(CharSequence text,int start,int count,int after){}public void onTextChanged(CharSequence text,int start,int before,int count){updateConnect.run();}public void afterTextChanged(android.text.Editable text){}};host.addTextChangedListener(changes);remotePort.addTextChangedListener(changes);updateConnectDialog=updateConnect;dialog.setOnDismissListener(ignored->{if(updateConnectDialog==updateConnect)updateConnectDialog=null;});updateConnect.run();
    }
    private void preparePairing(String expected,String address,int port,String local,boolean open) throws Exception {
        if(pairingDialog!=null||connectedEndpoint(address,""+port))return;
        for(String[] pending:peers.pendingForgets())if(pending.length>=4&&((!expected.isEmpty()&&expected.equals(pending[0]))||endpoint(address,""+port).equals(endpoint(pending[2],pending[3]))))throw new IllegalStateException("Pairing removal is pending for this device. Finish or dismiss it before pairing again.");
        String identity=expected;boolean remembered=false;
        for(String[] saved:peers.remembered())if(saved.length>=4 && ((!identity.isEmpty()&&identity.equals(saved[0])) || (identity.isEmpty()&&endpoint(address,""+port).equals(endpoint(saved[2],saved[3]))))) {identity=saved[0];remembered=true;break;}
        if(!remembered&&!expected.isEmpty())for(String[] saved:peers.remembered())if(saved.length>=4&&endpoint(address,""+port).equals(endpoint(saved[2],saved[3])))throw new IllegalStateException("This address belongs to a saved device with a different identity. Check the address or remove its old pairing first.");
        PairingAttempt attempt=new PairingAttempt(address,port,local,identity,remembered,open);
        if(!attempt.codeRequired){submitPairing(attempt,"");return;}
        showPairingPrompt(attempt,address+":"+port);
    }
    private void showPairingPrompt(PairingAttempt attempt,String description) {
        if(pairingDialog!=null)return;
        LinearLayout form=column();form.setPadding(dp(24),dp(12),dp(24),dp(12));form.setBackgroundColor(BG);
        form.addView(note(description));
        EditText secret=deviceField("Six-digit pairing code","",true);
        secret.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD);
        secret.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(6)});
        secret.setSaveEnabled(false);
        form.addView(note("Enter the code shown on the other device."));form.addView(secret,full());
        android.app.AlertDialog dialog=new android.app.AlertDialog.Builder(this).setTitle("Pairing code").setView(form).setPositiveButton("Connect",null).setNegativeButton("Cancel",null).create();
        pairingDialog=dialog;
        dialog.setOnDismissListener(ignored->{attempt.cancel();secret.setText("");if(pairingDialog==dialog)pairingDialog=null;});
        dialog.setOnShowListener(ignored->dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener(view->{
            String entered=secret.getText().toString();
            if(attempt.codeRequired&&!entered.matches("[0-9]{6}")){secret.setError("Enter the six-digit pairing code");secret.requestFocus();return;}
            dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setEnabled(false);
            try {submitPairing(attempt,entered);}catch(Exception error){status(error.getMessage()==null?"Device connection failed":error.getMessage());}
            finally {secret.setText("");dialog.dismiss();}
        }));dialog.show();
    }
    private void submitPairing(PairingAttempt attempt,String code) throws Exception {
        if(!foreground||peers==null||!attempt.submit(code))return;
        long token=attempt.gateway>0?peers.connectVia(attempt.gateway,attempt.identity,code):peers.connect(attempt.address,attempt.port,attempt.local,attempt.identity,code,attempt.open);
        if(attempt.gateway==0)peerEndpoints.put(token,endpoint(attempt.address,""+attempt.port));pendingPeers.put(token,attempt.identity);refreshTrusted();renderNearby();
    }
    private void cancelSearch(){++searchEpoch;if(nearbySearch!=null){nearbySearch.close();nearbySearch=null;}if(searchDevices!=null)searchVisual(false);}
    private void stopKnownPeerObserver(){++observerEpoch;if(knownPeerObserver!=null){knownPeerObserver.close();knownPeerObserver=null;}}
    private void observeKnownPeers(){
        if(!foreground||peers==null||!sharingLifecycle.listening()||knownPeerObserver!=null||sharingAddress.startsWith("127."))return;
        safely(()->{Set<String> known=new HashSet<>();for(String[] remembered:peers.remembered())if(remembered.length>0)known.add(remembered[0]);for(String[] pending:peers.pendingForgets())if(pending.length>0)known.add(pending[0]);if(known.isEmpty())return;
            long epoch=observerEpoch;knownPeerObserver=new NearbyPeerSearch(known);knownPeerObserver.start(this,sharingAddress,(hints,error)->handler.post(()->{if(!foreground||epoch!=observerEpoch)return;knownPeerObserver=null;if(error==null)safely(()->{for(NearbyPeers.Hint hint:hints.visible(peers.identity(),Set.of(),android.os.SystemClock.elapsedRealtime()))peers.observeRoute(hint.id(),hint.address(),hint.port());});}));
        });
    }
    private void searchNearby(){
        if(peers==null||!foreground)return;cancelSearch();nearby=new NearbyPeers();renderNearby();searchVisual(true);searchStatus.setText("Searching this network");long epoch=searchEpoch;
        String local=interfaces.getSelectedItem()==null?"":interfaces.getSelectedItem().toString();nearbySearch=new NearbyPeerSearch();nearbySearch.start(this,local,new NearbyPeerSearch.Listener(){
            public void progress(NearbyPeers found){handler.post(()->{if(!foreground||epoch!=searchEpoch)return;nearby=found;renderNearby();});}
            public void finished(NearbyPeers found,String error){handler.post(()->{if(!foreground||epoch!=searchEpoch)return;nearbySearch=null;searchVisual(false);nearby=found;safely(()->{for(NearbyPeers.Hint hint:found.visible(peers.identity(),Set.of(),android.os.SystemClock.elapsedRealtime()))peers.observeRoute(hint.id(),hint.address(),hint.port());});searchStatus.setText(error==null?"Search complete. Refresh to search again.":error);renderNearby();});}
        });
    }
    private void renderNearby(){if(peers==null||nearbyRows==null)return;safely(()->{
        Set<String> known=new HashSet<>(connectedPeers.values());known.addAll(pendingPeers.values());for(String[] device:peers.remembered())if(device.length>0)known.add(device[0]);for(String[] device:peers.pendingForgets())if(device.length>0)known.add(device[0]);
        nearbyRows.removeAllViews();for(NearbyPeers.Hint hint:nearby.visible(peers.identity(),known,android.os.SystemClock.elapsedRealtime())){
            LinearLayout row=column();row.setPadding(dp(14),dp(8),dp(14),dp(8));row.setBackground(rounded(PANEL,BORDER));LinearLayout.LayoutParams params=full();params.bottomMargin=dp(12);nearbyRows.addView(row,params);LinearLayout identityRow=new LinearLayout(this);identityRow.setGravity(Gravity.CENTER_VERTICAL);ImageView deviceIcon=new ImageView(this);deviceIcon.setImageDrawable(new LineIcon("Devices"));identityRow.addView(deviceIcon,new LinearLayout.LayoutParams(dp(32),dp(32)));TextView deviceName=label(hint.name(),18);deviceName.setPadding(dp(12),dp(8),0,dp(8));deviceName.setTypeface(null,Typeface.BOLD);identityRow.addView(deviceName,new LinearLayout.LayoutParams(0,-2,1));row.addView(identityRow,full());row.addView(note(hint.address()+":"+hint.port()));if(!hint.pairingRequired())row.addView(note("Pairing disabled ? identity will not be verified"));row.addView(button("Connect",()->safely(()->preparePairing(hint.id(),hint.address(),hint.port(),interfaces.getSelectedItem()==null?"":interfaces.getSelectedItem().toString(),!hint.pairingRequired()))));
        }
    });}
    private void renderEndpointDevices(){if(peers==null||endpointRows==null)return;safely(()->{
        endpointRows.removeAllViews();Set<String> known=new HashSet<>(connectedPeers.values());known.addAll(pendingPeers.values());known.add(peers.identity());
        for(String[] saved:peers.remembered())if(saved.length>0)known.add(saved[0]);for(String[] pending:peers.pendingForgets())if(pending.length>0)known.add(pending[0]);
        Set<String> shown=new HashSet<>();
        for(Map.Entry<Long,Map<String,String>> route:endpointsVia.entrySet())if(connectedPeers.containsKey(route.getKey()))for(Map.Entry<String,String> endpoint:route.getValue().entrySet()){
            if(known.contains(endpoint.getKey())||!shown.add(endpoint.getKey()))continue;
            final long gateway=route.getKey();final String identity=endpoint.getKey(),name=endpoint.getValue();final String gatewayName=peerName(connectedPeers.get(gateway));
            LinearLayout card=column();card.setPadding(dp(14),dp(8),dp(14),dp(8));card.setBackground(rounded(PANEL,BORDER));LinearLayout.LayoutParams spacing=full();spacing.bottomMargin=dp(12);endpointRows.addView(card,spacing);
            card.addView(label(name,18));card.addView(note("Via "+gatewayName+". Pair with this device so it can securely request this phone's resources."));
            card.addView(button("Pair device",()->showPairingPrompt(PairingAttempt.via(gateway,identity),name+" via "+gatewayName+". Enter the code shown on "+name+", not the forwarding device.")));
        }
        endpointSection.setVisibility(endpointRows.getChildCount()>0?View.VISIBLE:View.GONE);
    });}
    private void refreshTrusted(){if(peers==null)return;if(updateConnectDialog!=null)updateConnectDialog.run();safely(()->{
        trustedRows.removeAllViews();unpairedRows.removeAllViews();
        Map<String,String[]> listed=new LinkedHashMap<>();Set<String> rememberedIds=new HashSet<>();for(String[] entry:peers.remembered())if(entry.length>=2){listed.put(entry[0],entry);rememberedIds.add(entry[0]);}for(String id:connectedPeers.values())listed.putIfAbsent(id,new String[]{id,peerName(id)});Set<String> removals=new HashSet<>();for(String[] entry:peers.pendingForgets())if(entry.length>=2){listed.put(entry[0],entry);removals.add(entry[0]);}
        for(String[] device:listed.values())if(device.length>=2){
            String id=device[0],name=device[1];boolean expanded=deviceExpanded.getOrDefault(id,true);
            LinearLayout card=column();card.setPadding(dp(14),dp(8),dp(14),dp(8));card.setBackground(rounded(PANEL,BORDER));LinearLayout.LayoutParams params=full();params.bottomMargin=dp(12);boolean unpaired=!rememberedIds.contains(id)&&!removals.contains(id);(unpaired?unpairedRows:trustedRows).addView(card,params);
            LinearLayout header=new LinearLayout(this);header.setGravity(Gravity.CENTER_VERTICAL);header.setMinimumHeight(dp(56));header.setClickable(true);header.setFocusable(true);
            ImageView deviceIcon=new ImageView(this);deviceIcon.setImageDrawable(new LineIcon("Devices"));deviceIcon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);header.addView(deviceIcon,new LinearLayout.LayoutParams(dp(32),dp(32)));TextView title=label(name,18);title.setPadding(dp(12),dp(8),dp(12),dp(8));title.setTypeface(null,Typeface.BOLD);title.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);header.addView(title,new LinearLayout.LayoutParams(0,-2,1));
            ImageView arrow=new ImageView(this);arrow.setImageDrawable(new LineIcon("chevron"));arrow.setRotation(expanded?90:0);arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);header.addView(arrow,new LinearLayout.LayoutParams(dp(24),dp(24)));card.addView(header,full());
            LinearLayout details=column();details.setVisibility(expanded?View.VISIBLE:View.GONE);card.addView(details,full());
            if(device.length>=4&&!device[2].isBlank())details.addView(note(device[2]+":"+device[3]));
            if(removals.contains(id))details.addView(note("Pairing removed locally; waiting for the other device"));
            else {if(unpaired)details.addView(note("Unpaired ? identity is not verified"));details.addView(button("Resources",()->selectPage("Resources")));}
            if(removals.contains(id))details.addView(button("Dismiss pending removal",()->new android.app.AlertDialog.Builder(this).setTitle("Dismiss pending removal?").setMessage("The other device has not confirmed removal. Its saved pairing may remain. This does not restore trust on this phone; remove the pairing on the other device before pairing again.").setPositiveButton("Dismiss",(dialog,which)->safely(()->peers.dismissForget(id))).setNegativeButton("Cancel",null).show()));

            if(rememberedIds.contains(id))details.addView(button("Forget device",()->new android.app.AlertDialog.Builder(this).setTitle("Forget this device?").setMessage("Its active connections will close and the pairing will be removed from both devices. If the other device is offline, removal remains pending until it responds. Older versions may require removing the pairing there manually.").setPositiveButton("Forget",(dialog,which)->safely(()->peers.forget(id))).setNegativeButton("Cancel",null).show()));
            header.setContentDescription(name);header.setStateDescription(expanded?"Expanded":"Collapsed");
            header.setAccessibilityDelegate(new View.AccessibilityDelegate(){@Override public void onInitializeAccessibilityNodeInfo(View host,android.view.accessibility.AccessibilityNodeInfo info){super.onInitializeAccessibilityNodeInfo(host,info);info.setClassName(Button.class.getName());}});
            header.setOnClickListener(view->{boolean open=!deviceExpanded.getOrDefault(id,true);deviceExpanded.put(id,open);details.setVisibility(open?View.VISIBLE:View.GONE);arrow.animate().rotation(open?90:0).setDuration(160).start();header.setStateDescription(open?"Expanded":"Collapsed");});
        }
        if(trustedRows.getChildCount()==0)trustedRows.addView(note("No paired devices yet. Search nearby devices or pair by address."));unpairedSection.setVisibility(unpairedRows.getChildCount()>0?View.VISIBLE:View.GONE);renderNearby();renderEndpointDevices();
    });}
    private void setFullscreen(boolean value){fullscreen=value;if(drawerOpen)showDrawer(false);toolbar.setVisibility(value?View.GONE:View.VISIBLE);status.setVisibility(value||status.getText().length()==0?View.GONE:View.VISIBLE);selectPage(selectedPage);fullscreenButton.setText(value?"Exit fullscreen":"Fullscreen");fitVideo();}
    private void fitVideo(){int width=Math.max(dp(120),page.getWidth()-page.getPaddingLeft()-page.getPaddingRight());int height=Math.max(1,width*streamHeight/Math.max(1,streamWidth));int maximum=fullscreen?Math.max(dp(120),scroll.getHeight()-dp(88)):dp(340);if(height>maximum){height=maximum;width=Math.max(1,height*streamWidth/Math.max(1,streamHeight));}surface.setLayoutParams(new LinearLayout.LayoutParams(width,height));}
    @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);FrameLayout.LayoutParams params=(FrameLayout.LayoutParams)drawer.getLayoutParams();params.width=Math.min(dp(300),getResources().getDisplayMetrics().widthPixels-dp(56));drawer.setLayoutParams(params);cancelDrawerAnimation();drawerSwipe.cancel();drawerSwipeConsumed=false;setDrawerProgress(drawerOpen?1:0);page.post(this::fitVideo);}
    @android.annotation.SuppressLint("GestureBackNavigation") // API 30-32 fallback; 33+ uses OnBackInvokedDispatcher.
    @Override public void onBackPressed(){back();}
    private void back(){if(drawerOpen){showDrawer(false);return;}if(fullscreen){setFullscreen(false);return;}String previous=pageHistory.back();if(previous!=null){renderPage(previous);return;}if(!activeRows.isEmpty()){Toast.makeText(this,"Disconnect active resources before leaving ExtraPane.",Toast.LENGTH_SHORT).show();return;}finish();}
    private String opaque(String local){android.content.SharedPreferences settings=getSharedPreferences("peer-offer-identifiers",MODE_PRIVATE);String id=settings.getString(local,null);if(id==null){id=UUID.randomUUID().toString();settings.edit().putString(local,id).apply();}return id;}
    private boolean available(Offered item){String permission=item.kind()==1?Manifest.permission.CAMERA:item.kind()==2?Manifest.permission.RECORD_AUDIO:null;return permission==null||checkSelfPermission(permission)==PackageManager.PERMISSION_GRANTED;}
    private String peerName(String id){if(peers!=null)try{for(String[] remembered:peers.remembered())if(remembered.length>=2&&remembered[0].equals(id))return remembered[1];}catch(Exception ignored){}return "Device "+id.substring(0,Math.min(8,id.length()));}
    private void grantResourcePermission(int kind){
        String permission=kind==1?Manifest.permission.CAMERA:Manifest.permission.RECORD_AUDIO;
        android.content.SharedPreferences preferences=getSharedPreferences("permission-prompts",MODE_PRIVATE);
        if(preferences.getBoolean(permission,false)&&!shouldShowRequestPermissionRationale(permission)){
            startActivity(new android.content.Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,android.net.Uri.parse("package:"+getPackageName())));
        }else{preferences.edit().putBoolean(permission,true).apply();requestPermissions(new String[]{permission},kind);}
    }
    private void addOffer(String local,String label,int kind,Object source){
        String id=opaque(local);PowerButton power=new PowerButton(label);ResourceGroup group=offerGroups.get(kind);group.body.addView(resourceRow(label,power),full());TextView availability=note("");group.body.addView(availability,full());Offered item=new Offered(id,label,kind,source,power,availability);offered.put(id,item);
        power.setOnClickListener(view->disconnectResource(id));
        if(!available(item)){Button grant=button("Allow "+(kind==1?"camera":"microphone")+" access",()->grantResourcePermission(kind));group.body.addView(grant,full());}
    }
    private void updateOfferStates(){for(Offered item:offered.values()){List<ProviderSessions.Session> sessions=providerSessions.forResource(item.id());boolean active=!sessions.isEmpty();item.selected().setChecked(active);item.selected().setEnabled(active);item.selected().setAlpha(active?1f:.35f);item.selected().setContentDescription(active?"Disconnect "+item.label():"Available for another device to request");item.selected().setStateDescription(active?"In use":available(item)?"Available":"Permission required");Set<String> users=new LinkedHashSet<>();for(ProviderSessions.Session session:sessions)users.add(peerName(session.peer()));item.availability().setText(active?"Used by "+String.join(", ",users):available(item)?(foreground&&sharingLifecycle.listening()?"Available":"Waiting for network"):"Permission required");}updateSharingControls();}
    private void disconnectResource(String resource){for(ProviderSessions.Session session:providerSessions.forResource(resource))disconnectSession(session);}
    private void disconnectSession(ProviderSessions.Session session){stopNative(session.key());if(peers!=null)safely(()->peers.closeResource(session.connection(),session.grant()));}
    private void disconnectAll(){for(ProviderSessions.Session session:providerSessions.all())disconnectSession(session);status("Resources disconnected; this device is still available");}
    private void refreshOffers(){if(inputAllowed.getParent() instanceof ViewGroup oldParent)oldParent.removeView(inputAllowed);offerRows.removeAllViews();offerGroups.clear();offered.clear();for(int kind:new int[]{0,1,4,2,3})offerGroups.put(kind,new ResourceGroup(groupName(kind),offerRows));addOffer("display","This phone's display",0,null);offerGroups.get(0).body.addView(resourceRow("Allow input",inputAllowed),full());Point size=new Point();getDisplay().getRealSize(size);offerGroups.get(0).body.addView(note(size.x+" x "+size.y+" / "+Math.round(getDisplay().getRefreshRate())+" Hz. Shows a virtual monitor created on your other computer."));
        try{for(CameraCapture.Choice choice:CameraCapture.choices(this))addOffer("camera:"+choice.id(),choice.toString(),1,choice);}catch(Exception e){offerGroups.get(1).body.addView(note("Camera inventory unavailable: "+e.getMessage()));}
        for(SpeakerSession.Choice choice:SpeakerSession.choices(this))addOffer("speaker:"+choice.id(),choice.label(),4,choice);
        for(MicrophoneCapture.Choice choice:MicrophoneCapture.choices(this))addOffer("microphone:"+choice.id(),choice.label(),2,choice);
        for(Map.Entry<Integer,ResourceGroup> entry:offerGroups.entrySet()){int count=0;for(Offered item:offered.values())if(item.kind()==entry.getKey())++count;entry.getValue().count(count);if(count==0)entry.getValue().body.addView(note(entry.getKey()==3?"USB sharing is not available on Android.":"No available devices."));}publish();updateOfferStates();}
    private void updateConnectionGroups(){int total=0;for(ResourceGroup group:activeGroups.values()){int count=group.body.getChildCount();group.count(count);group.box.setVisibility(count>0?View.VISIBLE:View.GONE);total+=count;}connectionEmpty.setVisibility(total==0?View.VISIBLE:View.GONE);refreshResources.setEnabled(true);updateOfferStates();}
    private boolean hasOffers(){for(Offered item:offered.values())if(available(item))return true;return false;}
    private void updateSharingControls(){boolean active=sharingLifecycle.active();sharing.setVisibility(!providerSessions.all().isEmpty()?View.VISIBLE:View.GONE);sharing.setEnabled(true);interfaces.setEnabled(providerSessions.all().isEmpty());port.setEnabled(providerSessions.all().isEmpty());applyNetwork.setEnabled(providerSessions.all().isEmpty());}
    private void publishSelected()throws java.io.IOException{
        ArrayList<String[]> rows=new ArrayList<>();Point size=new Point();getDisplay().getRealSize(size);int hz=Math.max(1,Math.min(1000,Math.round(getDisplay().getRefreshRate())));
        for(Offered item:offered.values())if(available(item)){int width=item.kind()==0?size.x:0,height=item.kind()==0?size.y:0;rows.add(new String[]{item.id(),item.label(),""+item.kind(),"true",item.kind()==0&&inputAllowed.isChecked()?"true":"false",""+width,""+height,item.kind()==0?""+hz:"0",""});}
        peers.offers(rows.toArray(new String[0][]));
    }
    private void publish(){if(peers==null)return;try{publishSelected();}catch(Exception e){stopSharing();status(e.getMessage()==null?"Device sharing failed":e.getMessage());}updateSharingControls();}
    private void ensureListening(){
        try{
            if(peers==null||!foreground||listenFailed)return;

            if(!sharingLifecycle.active()){
                int number;try{number=Integer.parseInt(port.getText().toString().trim());}catch(NumberFormatException e){throw new IllegalArgumentException("Enter a port from 1 to 65535");}
                if(number<1||number>65535)throw new IllegalArgumentException("Enter a port from 1 to 65535");
                int index=interfaces.getSelectedItemPosition();if(index<0||index>=addresses.size())throw new IllegalArgumentException("Select a local network address");
                String address=addresses.get(index);InetAddress local=InetAddress.getByName(address);NetworkInterface nic=NetworkInterface.getByInetAddress(local);
                if(nic==null||!nic.isUp())throw new IllegalArgumentException("The selected network address is no longer available");
                publishSelected();sharingAddress=address;sharingLifecycle.begin(peers.start(address,number));status("Starting sharing...");
            }
        }catch(Exception e){
            // Retry a configuration/bind failure only after Apply, resume, or a real network change.
            listenFailed=true;failedNetworks=new ArrayList<>(addresses);stopSharing();status(e.getMessage()==null?"Device sharing failed":e.getMessage());
        }
        updateSharingControls();
    }
    private void applyNetworkSettings(){
        if(!providerSessions.all().isEmpty())return;
        try{int requested=Integer.parseInt(port.getText().toString().trim());if(requested<1||requested>65535)throw new NumberFormatException();}
        catch(NumberFormatException e){port.setError("Enter a port from 1 to 65535");return;}
        listenFailed=false;stopSharing();refreshInterfaces();ensureListening();
    }
    private void maintainAvailability()throws Exception{
        if(!foreground||peers==null)return;
        if(sharingLifecycle.active()){
            NetworkInterface nic=NetworkInterface.getByInetAddress(InetAddress.getByName(sharingAddress));
            if(nic==null||!nic.isUp())stopSharing();
            else if(providerSessions.all().isEmpty()&&sharingAddress.equals("127.0.0.1")){
                refreshInterfaces();if(!addresses.contains("127.0.0.1"))stopSharing();
            }
        }
        if(!sharingLifecycle.active()){refreshInterfaces();if(listenFailed&&!failedNetworks.equals(addresses))listenFailed=false;ensureListening();}
    }
    private void stopSharing(){
        stopKnownPeerObserver();sharingLifecycle.stop();providerSessions.clear();
        if(fullscreen)setFullscreen(false);if(beacon!=null){beacon.close();beacon=null;}
        if(peers!=null){safely(()->peers.offers(new String[0][]));safely(()->peers.stop());}
        for(Runnable cancel:new ArrayList<>(cancellations.values()))cancel.run();cancellations.clear();activeRows.clear();for(ResourceGroup group:activeGroups.values())group.body.removeAllViews();updateConnectionGroups();pendingDisplay=null;displayEvent=null;videoActive=false;displayKey=null;input.enable(false);videoArea.setVisibility(View.GONE);updateSharingControls();status("Device unavailable while ExtraPane is paused");
    }
    private final Runnable poll=new Runnable(){public void run(){if(!foreground||peers==null)return;safely(()->{for(String[] event:peers.poll())event(event);});if(android.os.SystemClock.elapsedRealtime()>=nextListenAttempt){nextListenAttempt=android.os.SystemClock.elapsedRealtime()+3000;safely(PeerActivity.this::maintainAvailability);observeKnownPeers();}handler.postDelayed(this,30);}};
    private void event(String[] event)throws Exception{if(event.length<15)return;switch(event[0]){
        case "listening" -> {if(!foreground||!hasOffers()||event.length<=18||!sharingLifecycle.confirm(Long.parseLong(event[18])))return;updateOfferStates();status("Listening on "+sharingAddress+":"+event[6]);try{if(beacon!=null)beacon.close();beacon=new PeerBeacon(peers,sharingAddress,Integer.parseInt(event[6]));}catch(Exception e){status("Listening on "+sharingAddress+":"+event[6]+". LAN discovery unavailable; connect by address.");}}
        case "native-ready" -> {try{startNative(event);}catch(Exception e){ended(event,e.getMessage()==null?"Resource could not start":e.getMessage());}}
        case "native-stop" -> stopNative(key(event));
        case "error" -> {long failedToken=Long.parseLong(event[1]);pendingPeers.remove(failedToken);if(!connectedPeers.containsKey(failedToken))peerEndpoints.remove(failedToken);refreshTrusted();long generation=event.length>18?Long.parseLong(event[18]):0;if(generation!=0){if(!sharingLifecycle.matches(generation))return;listenFailed=true;failedNetworks=new ArrayList<>(addresses);stopSharing();}status(event[7]);}
        case "catalog-begin" -> endpointsVia.put(Long.parseLong(event[1]),new LinkedHashMap<>());
        case "endpoint" -> endpointsVia.computeIfAbsent(Long.parseLong(event[1]),ignored->new LinkedHashMap<>()).put(event[8],event[7]);
        case "catalog-end" -> renderEndpointDevices();
        case "disconnected" -> {long token=Long.parseLong(event[1]);endpointsVia.remove(token);connectedPeers.remove(token);pendingPeers.remove(token);peerEndpoints.remove(token);refreshTrusted();}
        case "connected" -> {long token=Long.parseLong(event[1]);pendingPeers.remove(token);connectedPeers.put(token,event[4]);refreshTrusted();}
        case "trust-changed" -> refreshTrusted();
        default -> {}
    }}
    private void stopNative(String key){providerSessions.remove(key);Runnable cancel=cancellations.remove(key);if(cancel!=null)cancel.run();View row=activeRows.remove(key);if(row!=null&&row.getParent() instanceof ViewGroup parent)parent.removeView(row);updateConnectionGroups();if(key.equals(displayKey)){if(fullscreen)setFullscreen(false);pendingDisplay=null;displayEvent=null;videoActive=false;displayKey=null;input.enable(false);videoArea.setVisibility(View.GONE);}}
    private void ended(String[] event,String reason){handler.post(()->{String key=key(event);if(!activeRows.containsKey(key)&&!key.equals(displayKey))return;stopNative(key);status(reason);if(peers!=null)safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));});}
    private void startNative(String[] event)throws Exception{String key=key(event);Offered item=offered.get(event[9]);if(!foreground||!sharingLifecycle.listening()||item==null||!available(item)){peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3]));return;}if(providerSessions.contains(key))return;providerSessions.add(new ProviderSessions.Session(Long.parseLong(event[1]),Long.parseLong(event[3]),item.id(),event[4]));
        PowerButton stop=new PowerButton(item.label());stop.setChecked(true);stop.setContentDescription("Disconnect "+item.label());stop.setTooltipText("Disconnect "+item.label());stop.setStateDescription("Connected");stop.setOnClickListener(view->{stopNative(key);safely(()->peers.closeResource(Long.parseLong(event[1]),Long.parseLong(event[3])));});LinearLayout row=resourceRow(item.label()+" ? "+peerName(event[4]),stop);activeGroups.get(item.kind()).body.addView(row,full());activeRows.put(key,row);updateConnectionGroups();
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
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results){super.onRequestPermissionsResult(request,permissions,results);if(foreground)safely(()->{refreshOffers();ensureListening();});}
    @Override protected void onResume(){super.onResume();foreground=true;listenFailed=false;if(peers!=null)safely(()->{peers.setForeground(true);peers.setAutoConnect(true);refreshInterfaces();refreshOffers();ensureListening();});handler.post(poll);}
    @Override protected void onPause(){if(pairingDialog!=null)pairingDialog.dismiss();cancelSearch();cancelDrawerAnimation();drawerSwipe.cancel();drawerSwipeConsumed=false;if(drawerVelocity!=null){drawerVelocity.recycle();drawerVelocity=null;}if(drawer!=null)setDrawerProgress(drawerOpen?1:0);foreground=false;handler.removeCallbacks(poll);if(peers!=null)safely(()->{peers.setAutoConnect(false);peers.setForeground(false);});stopSharing();super.onPause();}
    @Override protected void onStop(){foreground=false;handler.removeCallbacks(poll);stopSharing();super.onStop();}
    @Override protected void onDestroy(){handler.removeCallbacks(poll);if(peers!=null){PeerService closing=peers;peers=null;new Thread(closing::close,"ExtraPane peer shutdown").start();}super.onDestroy();}
}
