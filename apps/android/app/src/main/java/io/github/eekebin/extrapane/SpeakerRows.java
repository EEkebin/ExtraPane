package io.github.eekebin.extrapane;

import android.app.Activity;
import android.content.*;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.view.View;
import android.widget.*;
import java.util.*;
import java.util.function.*;

/** Repeatable selected speaker/headset outputs, independent of capture resources. */
final class SpeakerRows {
    interface Notifications{void request(Runnable granted,Runnable denied);}
    private final Activity activity;private final LinearLayout group;private final Supplier<CaptureRows.Target> target;private final BooleanSupplier shared;
    private final Notifications notifications;private final Runnable changed,invalidShared;private final BiConsumer<Boolean,String> failure;
    private final LinkedHashMap<String,Row> rows=new LinkedHashMap<>();private final Handler main=new Handler(Looper.getMainLooper());
    private final Map<View,android.graphics.drawable.Drawable> backgrounds=new HashMap<>();private final Map<View,Runnable> timers=new HashMap<>();
    private final Button disconnectAll;private SpeakerService.LocalBinder binder;private boolean bound,visible;
    private final ServiceConnection connection=new ServiceConnection(){
        public void onServiceConnected(ComponentName name,IBinder value){binder=(SpeakerService.LocalBinder)value;binder.listen(SpeakerRows.this::snapshots);}
        public void onServiceDisconnected(ComponentName name){binder=null;for(Row row:rows.values()){row.active=row.connected=row.stopping=row.pending=false;row.render();}changed.run();}
    };
    SpeakerRows(Activity activity,LinearLayout group,Supplier<CaptureRows.Target> target,BooleanSupplier shared,Notifications notifications,Runnable changed,Runnable invalidShared,BiConsumer<Boolean,String> failure){
        this.activity=activity;this.group=group;this.target=target;this.shared=shared;this.notifications=notifications;this.changed=changed;this.invalidShared=invalidShared;this.failure=failure;
        LinearLayout actions=new LinearLayout(activity);actions.addView(button("+ Add speakers",()->add(UUID.randomUUID().toString()),false));disconnectAll=button("Disconnect all speakers",this::stopAll,true);disconnectAll.setVisibility(View.GONE);actions.addView(disconnectAll);group.addView(actions);add(UUID.randomUUID().toString());
    }
    private int dp(int value){return Math.round(value*activity.getResources().getDisplayMetrics().density);}
    private GradientDrawable background(int color){GradientDrawable b=new GradientDrawable();b.setColor(color);b.setCornerRadius(dp(10));return b;}
    private Button button(String text,Runnable action,boolean red){Button b=new Button(activity);b.setText(text);b.setAllCaps(false);b.setTextColor(Color.WHITE);b.setBackground(background(red?Color.rgb(179,38,53):Color.rgb(36,55,76)));b.setOnClickListener(v->action.run());return b;}
    private TextView label(String text){TextView v=new TextView(activity);v.setText(text);v.setTextColor(Color.rgb(211,229,242));v.setPadding(0,dp(6),0,dp(6));return v;}
    private EditText field(String hint,String value){EditText v=new EditText(activity);v.setSingleLine();v.setTextColor(Color.WHITE);v.setHintTextColor(Color.LTGRAY);v.setHint(hint);v.setText(value);return v;}
    private void toast(String value){Toast.makeText(activity,value,Toast.LENGTH_LONG).show();}
    private void mark(View v){if(!backgrounds.containsKey(v))backgrounds.put(v,v.getBackground());Runnable prior=timers.remove(v);if(prior!=null)main.removeCallbacks(prior);GradientDrawable b=background(Color.rgb(18,28,42));b.setStroke(dp(2),Color.rgb(255,101,119));v.setBackground(b);Runnable restore=()->{v.setBackground(backgrounds.remove(v));timers.remove(v);};timers.put(v,restore);main.postDelayed(restore,5000);}
    private Row add(String id){Row row=new Row(id);rows.put(id,row);return row;}
    private final class Row{
        final String id;final CaptureAttempt attempts=new CaptureAttempt();final LinearLayout root,configuration,destination;final PowerButton enable;final Spinner output;final EditText address,port,code;final TextView summary,status;final CheckBox mute;final SeekBar volume;final Button remove;
        final ArrayList<SpeakerSession.Choice> choices=new ArrayList<>();boolean active,connected,stopping,pending,updating;int selected=-1;CaptureRows.Target endpoint;String detail="",lastError="";
        Row(String id){
            this.id=id;root=new LinearLayout(activity);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(10),dp(12),dp(10),dp(12));group.addView(root);
            enable=new PowerButton(activity,"speakers",()->{if(active||pending)stop();else start();});root.addView(enable);configuration=new LinearLayout(activity);configuration.setOrientation(LinearLayout.VERTICAL);root.addView(configuration);
            output=new Spinner(activity,Spinner.MODE_DROPDOWN){@Override public boolean performClick(){refresh();return super.performClick();}};configuration.addView(output);
            destination=new LinearLayout(activity);destination.setOrientation(LinearLayout.VERTICAL);configuration.addView(destination);CaptureRows.Target common=target.get();address=field("Server IPv4 address",common.address());port=field("TCP port",Integer.toString(common.port()));port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);code=field("Access code (optional)","");code.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD);code.setSaveEnabled(false);code.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(6)});destination.addView(address);destination.addView(port);destination.addView(code);destination.addView(label("Uses the selected network above."));
            summary=label("");status=label("");root.addView(summary);root.addView(status);mute=new CheckBox(activity);mute.setText("Mute speakers");mute.setTextColor(Color.WHITE);mute.setOnCheckedChangeListener((b,value)->{if(!updating&&active)action(id,value?SpeakerService.MUTE:SpeakerService.UNMUTE,100);});root.addView(mute);
            root.addView(label("Volume"));volume=new SeekBar(activity);volume.setMax(100);volume.setProgress(100);volume.setContentDescription("Speaker volume");volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar bar,int value,boolean user){if(user&&active)action(id,SpeakerService.VOLUME,value);}public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){}});root.addView(volume);
            remove=button("Remove",()->{if(!active&&!pending){rows.remove(id);group.removeView(root);}},false);root.addView(remove);refresh();render();
        }
        void refresh(){int index=output.getSelectedItemPosition();if(index>=0&&index<choices.size())selected=choices.get(index).id();choices.clear();List<String> names=new ArrayList<>();int match=selected<0?0:-1;try{choices.addAll(SpeakerSession.choices(activity));for(int i=0;i<choices.size();i++){names.add(choices.get(i).label());if(choices.get(i).id()==selected)match=i;}}catch(RuntimeException e){status.setText("Output inventory unavailable: "+e.getMessage());}if(names.isEmpty())names.add("No output available");output.setAdapter(new ArrayAdapter<>(activity,android.R.layout.simple_spinner_dropdown_item,names));output.setSelection(match);}
        void render(){enable.state(active||pending,stopping);configuration.setVisibility(active||pending?View.GONE:View.VISIBLE);destination.setVisibility(shared.getAsBoolean()?View.GONE:View.VISIBLE);summary.setVisibility(active||pending?View.VISIBLE:View.GONE);summary.setText((stopping?"Stopping ":connected?"Connected to ":"Connecting to ")+(endpoint==null?"":endpoint.label())+"\n"+detail);remove.setEnabled(!active&&!pending);mute.setEnabled(active&&!stopping);volume.setEnabled(active&&!stopping);}
        void stop(){if(pending)attempts.cancelPending();pending=false;if(active){stopping=true;action(id,SpeakerService.STOP,100);}render();changed.run();}
        void start(){
            if(active||pending||!visible)return;CaptureRows.Target common=target.get(),candidate=shared.getAsBoolean()?common:new CaptureRows.Target(address.getText().toString().trim(),ConnectionValidation.number(port.getText().toString()),common.local(),common.network(),code.getText().toString());boolean valid=true;int index=output.getSelectedItemPosition();
            if(!ConnectionValidation.ipv4(candidate.address())){if(!shared.getAsBoolean())mark(address);valid=false;}if(candidate.port()<1||candidate.port()>65535){if(!shared.getAsBoolean())mark(port);valid=false;}if(!ConnectionValidation.code(candidate.code())){if(!shared.getAsBoolean())mark(code);valid=false;}if(index<0||index>=choices.size()){mark(output);valid=false;}
            if(!valid){if(shared.getAsBoolean())invalidShared.run();else toast("Check the highlighted connection fields.");return;}
            SpeakerSession.Choice choice=choices.get(index);selected=choice.id();detail=choice.label()+" - 48 kHz stereo";endpoint=new CaptureRows.Target(candidate.address(),candidate.port(),candidate.local(),candidate.network(),"");String attempt=attempts.begin();
            Intent request=new Intent(activity,SpeakerService.class).putExtra("row",id).putExtra("attempt",attempt).putExtra("address",candidate.address()).putExtra("port",candidate.port()).putExtra("local",candidate.local()).putExtra("network",candidate.network()).putExtra("code",candidate.code()).putExtra("output",choice.id()).putExtra("outputLabel",choice.label());pending=true;render();changed.run();
            notifications.request(()->{if(!attempts.current(attempt)||!pending||!visible){request.removeExtra("code");return;}try{activity.startForegroundService(request);code.setText("");active=true;pending=false;status.setText("Connecting...");bind();}catch(RuntimeException error){active=pending=false;request.removeExtra("code");status.setText("Cannot start speakers: "+error.getMessage());}render();changed.run();},()->{request.removeExtra("code");if(attempts.current(attempt)){pending=false;render();changed.run();}});
        }
        @SuppressWarnings("deprecation")void snapshot(SpeakerService.Snapshot snapshot){
            Intent config=snapshot.configuration();if(!attempts.accept(config.getStringExtra("attempt"),snapshot.active()))return;boolean wasConnected=connected;active=snapshot.active();connected=snapshot.connected();stopping=snapshot.stopping();pending=false;detail=snapshot.detail();endpoint=new CaptureRows.Target(config.getStringExtra("address"),config.getIntExtra("port",0),config.getStringExtra("local"),config.getParcelableExtra("network"),"");address.setText(endpoint.address());port.setText(Integer.toString(endpoint.port()));selected=config.getIntExtra("output",-1);output.setSelection(-1);for(int i=0;i<choices.size();i++)if(choices.get(i).id()==selected){output.setSelection(i);break;}
            status.setText(snapshot.status().equals("Disconnected")?"":snapshot.status());updating=true;mute.setChecked(snapshot.muted());volume.setProgress(snapshot.volume());updating=false;render();if(visible&&connected&&!wasConnected)toast("Speakers connected to "+snapshot.destination());
            if(!active&&!snapshot.status().equals(lastError)&&snapshot.status().toLowerCase(Locale.ROOT).matches(".*(access code|authentication|server access|admission).*")){lastError=snapshot.status();code.setText("");if(shared.getAsBoolean())failure.accept(true,snapshot.status());else{mark(code);toast("Check the highlighted access code.");failure.accept(false,snapshot.status());}}
        }
    }
    private void snapshots(List<SpeakerService.Snapshot> values){for(SpeakerService.Snapshot value:values){Row row=rows.get(value.id());if(row==null&&value.active())row=add(value.id());if(row!=null)row.snapshot(value);}changed.run();}
    private void action(String id,String action,int volume){Intent request=new Intent(activity,SpeakerService.class).setAction(action).putExtra("volume",volume);if(id!=null)request.putExtra("row",id);activity.startService(request);}
    private void bind(){if(!bound)bound=activity.bindService(new Intent(activity,SpeakerService.class),connection,Context.BIND_AUTO_CREATE);}
    void resume(){visible=true;bind();}
    void pause(){visible=false;for(Row row:rows.values()){if(row.pending)row.attempts.cancelPending();row.pending=false;}if(binder!=null){binder.listen(null);binder=null;}if(bound){activity.unbindService(connection);bound=false;}}
    void refreshLayout(){for(Row row:rows.values())row.render();disconnectAll.setVisibility(active()?View.VISIBLE:View.GONE);}
    void stopAll(){for(Row row:rows.values())row.stop();}
    boolean active(){for(Row row:rows.values())if(row.active||row.pending)return true;return false;}
    boolean matches(CaptureRows.Target target){for(Row row:rows.values())if((row.active||row.pending)&&(row.endpoint==null||!row.endpoint.key().equals(target.key())))return false;return true;}
}
