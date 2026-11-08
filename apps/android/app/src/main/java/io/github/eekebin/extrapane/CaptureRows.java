package io.github.eekebin.extrapane;

import android.app.Activity;
import android.content.*;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.net.Network;
import android.os.*;
import android.view.View;
import android.widget.*;
import java.util.*;
import java.util.function.*;

/** Repeatable capture rows. Workers and source ownership live in foreground services. */
final class CaptureRows {
    record Target(String address,int port,String local,Network network,String code){String key(){return address+":"+port+"@"+local+"/"+network;}String label(){return address+":"+port;}}
    interface Permissions {void request(boolean camera,Runnable granted,Runnable denied);}
    private final Activity activity;private final boolean camera;private final LinearLayout group;private final Supplier<Target> sharedTarget;private final BooleanSupplier shared;private final Permissions permissions;private final Runnable changed,invalidShared;private final BiConsumer<Boolean,String> failure;
    private final LinkedHashMap<String,Row> rows=new LinkedHashMap<>();private final Handler main=new Handler(Looper.getMainLooper());
    private final Map<View,android.graphics.drawable.Drawable> markBackgrounds=new HashMap<>();private final Map<View,Runnable> markTimers=new HashMap<>();
    private final Button disconnectAll;
    private CaptureShareService.LocalBinder binder;private boolean bound,visible;private final Class<?> service;
    private final ServiceConnection connection=new ServiceConnection(){
        public void onServiceConnected(ComponentName name,IBinder value){binder=(CaptureShareService.LocalBinder)value;binder.listen(CaptureRows.this::snapshots);}
        public void onServiceDisconnected(ComponentName name){binder=null;for(Row row:rows.values()){row.active=false;row.stopping=false;row.pending=false;row.connected=false;row.render();}changed.run();}
    };
    CaptureRows(Activity activity,boolean camera,LinearLayout group,Supplier<Target> target,BooleanSupplier shared,Permissions permissions,Runnable changed,Runnable invalidShared,BiConsumer<Boolean,String> failure){
        this.activity=activity;this.camera=camera;this.group=group;sharedTarget=target;this.shared=shared;this.permissions=permissions;this.changed=changed;this.invalidShared=invalidShared;this.failure=failure;service=camera?CameraShareService.class:MicrophoneShareService.class;
        LinearLayout actions=new LinearLayout(activity);actions.addView(button("+ Add "+kind().toLowerCase(Locale.ROOT),()->add(UUID.randomUUID().toString()),false));disconnectAll=button("Disconnect all",this::stopAll,true);disconnectAll.setVisibility(View.GONE);actions.addView(disconnectAll);group.addView(actions);
        add(UUID.randomUUID().toString());
    }
    private String kind(){return camera?"Camera":"Microphone";}
    private int dp(int value){return Math.round(value*activity.getResources().getDisplayMetrics().density);}
    private GradientDrawable background(int color){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(10));return d;}
    private Button button(String text,Runnable action,boolean red){Button result=new Button(activity);result.setText(text);result.setTextColor(Color.WHITE);result.setAllCaps(false);result.setBackground(background(red?Color.rgb(179,38,53):Color.rgb(36,55,76)));result.setOnClickListener(v->action.run());return result;}
    private TextView label(String text){TextView result=new TextView(activity);result.setText(text);result.setTextColor(Color.rgb(211,229,242));result.setPadding(0,dp(6),0,dp(6));return result;}
    private EditText field(String hint,String value){EditText result=new EditText(activity);result.setSingleLine();result.setTextColor(Color.WHITE);result.setHintTextColor(Color.LTGRAY);result.setHint(hint);result.setText(value);return result;}
    private void toast(String text){Toast.makeText(activity,text,Toast.LENGTH_LONG).show();}
    private void mark(View field){if(!markBackgrounds.containsKey(field))markBackgrounds.put(field,field.getBackground());Runnable previous=markTimers.remove(field);if(previous!=null)main.removeCallbacks(previous);GradientDrawable color=background(Color.rgb(18,28,42));color.setStroke(dp(2),Color.rgb(255,101,119));field.setBackground(color);Runnable restore=()->{field.setBackground(markBackgrounds.remove(field));markTimers.remove(field);};markTimers.put(field,restore);main.postDelayed(restore,5000);}
    private Row add(String id){Row row=new Row(id);rows.put(id,row);return row;}
    private final class Row {
        final CaptureAttempt attempts=new CaptureAttempt();final String id;final LinearLayout root,configuration,destination;final PowerButton enable;final CheckBox mute;final Spinner source;final EditText address,port,code;final TextView summary,status;final Button remove;
        final ArrayList<CameraCapture.Choice> cameras=new ArrayList<>();final ArrayList<MicrophoneCapture.Choice> microphones=new ArrayList<>();
        boolean active,connected,stopping,pending,updating;Target target;String detail="",selectedSource="",lastError="";
        Row(String id){
            this.id=id;root=new LinearLayout(activity);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(10),dp(12),dp(10),dp(12));group.addView(root);
            enable=new PowerButton(activity,kind(),()->{if(active||pending)stop();else start();});root.addView(enable);
            configuration=new LinearLayout(activity);configuration.setOrientation(LinearLayout.VERTICAL);root.addView(configuration);
            source=new Spinner(activity,Spinner.MODE_DROPDOWN){@Override public boolean performClick(){refresh();return super.performClick();}};configuration.addView(source);
            destination=new LinearLayout(activity);destination.setOrientation(LinearLayout.VERTICAL);configuration.addView(destination);
            Target initial=sharedTarget.get();address=field("Server IPv4 address",initial.address());port=field("TCP port",Integer.toString(initial.port()));port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);code=field("Access code (optional)","");code.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD);code.setSaveEnabled(false);code.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(6)});
            destination.addView(address);destination.addView(port);destination.addView(code);destination.addView(label("Uses the selected network above."));
            summary=label("");root.addView(summary);status=label("");root.addView(status);
            mute=new CheckBox(activity);mute.setText("Mute microphone");mute.setTextColor(Color.WHITE);if(!camera)root.addView(mute);mute.setOnCheckedChangeListener((b,value)->{if(!updating&&active)action(id,value?CaptureShareService.MUTE:CaptureShareService.UNMUTE);});
            LinearLayout actions=new LinearLayout(activity);remove=button("Remove",()->{if(!active&&!pending){rows.remove(id);group.removeView(root);}},false);actions.addView(remove);root.addView(actions);
            refresh();render();
        }
        void refresh(){
            int index=source.getSelectedItemPosition();if(camera&&index>=0&&index<cameras.size())selectedSource=cameras.get(index).id();if(!camera&&index>=0&&index<microphones.size())selectedSource=Integer.toString(microphones.get(index).id());
            cameras.clear();microphones.clear();ArrayList<String> labels=new ArrayList<>();int selected=selectedSource.isEmpty()?0:-1;
            try{if(camera){cameras.addAll(CameraCapture.choices(activity));for(int i=0;i<cameras.size();i++){CameraCapture.Choice c=cameras.get(i);labels.add(c.toString());if(c.id().equals(selectedSource))selected=i;}}else{microphones.addAll(MicrophoneCapture.choices(activity));for(int i=0;i<microphones.size();i++){MicrophoneCapture.Choice m=microphones.get(i);labels.add(m.label());if(Integer.toString(m.id()).equals(selectedSource))selected=i;}}}catch(Exception error){status.setText("Device inventory unavailable: "+error.getMessage());}
            if(labels.isEmpty())labels.add("No compatible "+kind().toLowerCase(Locale.ROOT)+" available");source.setAdapter(new ArrayAdapter<>(activity,android.R.layout.simple_spinner_dropdown_item,labels));source.setSelection(selected);
        }
        void render(){updating=true;enable.state(active||pending,stopping);configuration.setVisibility(active||pending?View.GONE:View.VISIBLE);destination.setVisibility(shared.getAsBoolean()?View.GONE:View.VISIBLE);summary.setVisibility(active||pending?View.VISIBLE:View.GONE);summary.setText((stopping?"Stopping ":connected?"Connected to ":"Connecting to ")+(target==null?"":target.label())+(detail.isEmpty()?"":"\n"+detail));remove.setEnabled(!active&&!pending);mute.setEnabled(active&&!stopping);updating=false;}
        void stop(){if(pending)attempts.cancelPending();pending=false;if(active){stopping=true;action(id,CaptureShareService.STOP);}render();changed.run();}
        void start(){
            if(active||pending||!visible){render();return;}
            Target common=sharedTarget.get();Target candidate=shared.getAsBoolean()?common:new Target(address.getText().toString().trim(),ConnectionValidation.number(port.getText().toString()),common.local(),common.network(),code.getText().toString());
            boolean valid=true;int index=source.getSelectedItemPosition();
            if(!ConnectionValidation.ipv4(candidate.address())){if(!shared.getAsBoolean())mark(address);valid=false;}
            if(candidate.port()<1||candidate.port()>65535){if(!shared.getAsBoolean())mark(port);valid=false;}
            if(!ConnectionValidation.code(candidate.code())){if(!shared.getAsBoolean())mark(code);valid=false;}
            if(index<0||(camera?index>=cameras.size():index>=microphones.size())){mark(source);valid=false;}
            if(!valid){if(shared.getAsBoolean())invalidShared.run();else toast("Check the highlighted connection fields.");render();changed.run();return;}
            // Freeze the selected source, destination and network before any permission prompt.
            String attempt=attempts.begin();Intent request=new Intent(activity,service).putExtra("attempt",attempt).putExtra("row",id).putExtra("address",candidate.address()).putExtra("port",candidate.port()).putExtra("local",candidate.local()).putExtra("network",candidate.network()).putExtra("code",candidate.code());
            if(camera){CameraCapture.Choice c=cameras.get(index);selectedSource=c.id();detail=c.toString();request.putExtra("camera",c.id()).putExtra("cameraLabel",c.label()).putExtra("width",c.width()).putExtra("height",c.height()).putExtra("fps",c.fps());}else{MicrophoneCapture.Choice m=microphones.get(index);selectedSource=Integer.toString(m.id());detail=m.label()+" - 48 kHz mono";request.putExtra("microphone",m.id()).putExtra("microphoneLabel",m.label());}
            target=new Target(candidate.address(),candidate.port(),candidate.local(),candidate.network(),"");pending=true;render();changed.run();
            permissions.request(camera,()->{if(!attempts.current(attempt)||!pending||!visible){request.removeExtra("code");if(attempts.current(attempt)){pending=false;render();changed.run();}return;}try{activity.startForegroundService(request);code.setText("");active=true;pending=false;status.setText("Connecting...");bind();}catch(RuntimeException error){active=false;pending=false;status.setText("Cannot start sharing: "+error.getMessage());request.removeExtra("code");}render();changed.run();},()->{request.removeExtra("code");if(!attempts.current(attempt))return;pending=false;status.setText("Capture permission was not granted");render();changed.run();});
        }
        @SuppressWarnings("deprecation") void snapshot(CaptureShareService.Snapshot value){
            if(!attempts.accept(value.configuration().getStringExtra("attempt"),value.active()))return;
            boolean wasConnected=connected;active=value.active();connected=value.connected();stopping=value.stopping();pending=false;detail=value.detail();Intent config=value.configuration();target=new Target(config.getStringExtra("address"),config.getIntExtra("port",0),config.getStringExtra("local"),config.getParcelableExtra("network"),"");address.setText(target.address());port.setText(Integer.toString(target.port()));selectedSource=camera?config.getStringExtra("camera"):Integer.toString(config.getIntExtra("microphone",-1));source.setSelection(-1);for(int i=0;i<(camera?cameras.size():microphones.size());i++){String identity=camera?cameras.get(i).id():Integer.toString(microphones.get(i).id());if(identity.equals(selectedSource)){source.setSelection(i);break;}}
            status.setText(value.status().equals("Disconnected")?"":value.status());updating=true;mute.setChecked(value.muted());updating=false;render();if(visible&&connected&&!wasConnected)toast(kind()+" connected to "+value.destination());
            if(!active&&!value.status().equals(lastError)&&value.status().toLowerCase(Locale.ROOT).matches(".*(access code|authentication|server access|admission).*")){lastError=value.status();code.setText("");if(shared.getAsBoolean())failure.accept(true,value.status());else {mark(code);toast("Check the highlighted access code.");failure.accept(false,value.status());}}
        }
    }
    private void snapshots(List<CaptureShareService.Snapshot> values){for(CaptureShareService.Snapshot value:values){Row row=rows.get(value.id());if(row==null&&value.active())row=add(value.id());if(row!=null)row.snapshot(value);}changed.run();}
    private void action(String id,String action){Intent request=new Intent(activity,service).setAction(action);if(id!=null)request.putExtra("row",id);activity.startService(request);}
    private void bind(){if(!bound)bound=activity.bindService(new Intent(activity,service),connection,Context.BIND_AUTO_CREATE);}
    void resume(){visible=true;bind();}
    void pause(){visible=false;for(Row row:rows.values()){if(row.pending)row.attempts.cancelPending();row.pending=false;}if(binder!=null){binder.listen(null);binder=null;}if(bound){activity.unbindService(connection);bound=false;}}
    void stopAll(){for(Row row:rows.values())row.stop();}
    void refreshLayout(){for(Row row:rows.values())row.render();disconnectAll.setVisibility(active()?View.VISIBLE:View.GONE);}
    boolean active(){for(Row row:rows.values())if(row.active||row.pending)return true;return false;}
    boolean matches(Target value){for(Row row:rows.values())if((row.active||row.pending)&&(row.target==null||!row.target.key().equals(value.key())))return false;return true;}
}
