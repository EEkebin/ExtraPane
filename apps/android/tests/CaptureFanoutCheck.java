package io.github.eekebin.extrapane;
import java.io.IOException;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
public final class CaptureFanoutCheck {
    record Packet(int value,boolean key){}
    static void require(boolean value){if(!value)throw new AssertionError("Capture fanout");}
    interface Operation {void run()throws Exception;}
    static void rejected(Operation action)throws Exception{try{action.run();throw new AssertionError("Expected fanout rejection");}catch(IOException expected){}}
    static final class Producer implements CaptureFanout.Producer<Packet> {
        final BlockingQueue<Packet> packets=new LinkedBlockingQueue<>();final CountDownLatch started=new CountDownLatch(1),closed=new CountDownLatch(1),finish=new CountDownLatch(1);boolean hold,failClose;
        public void start(){started.countDown();}public Packet next()throws Exception{Packet value=packets.take();if(value.value()<0)throw new IOException("simulated source failure");return value;}
        public void close()throws Exception{closed.countDown();if(hold)require(finish.await(3,TimeUnit.SECONDS));if(failClose)throw new IOException("simulated native cleanup failure");}
    }
    static Packet take(CaptureFanout<Packet>.Subscription subscription)throws Exception{for(int i=0;i<30;i++){Packet packet=subscription.next();if(packet!=null)return packet;}throw new AssertionError("No packet");}
    public static void main(String[] args)throws Exception{
        CaptureFanout<Packet> pool=new CaptureFanout<>(3,Packet::key);Producer producer=new Producer();AtomicInteger starts=new AtomicInteger();
        var first=pool.subscribe("camera","720p",()->{starts.incrementAndGet();return producer;});require(producer.started.await(2,TimeUnit.SECONDS));
        producer.packets.put(new Packet(1,true));require(take(first).value()==1);
        var second=pool.subscribe("camera","720p",()->{throw new AssertionError("Second capture opened");});
        producer.packets.put(new Packet(2,false));require(take(first).value()==2);require(second.next()==null);
        producer.packets.put(new Packet(3,true));require(take(first).value()==3&&take(second).value()==3&&starts.get()==1);
        rejected(()->pool.subscribe("camera","1080p",()->producer));
        // The slow destination fills independently while the first keeps consuming.
        for(int i=4;i<8;i++){producer.packets.put(new Packet(i,false));require(take(first).value()==i);}
        rejected(second::next);second.close();require(producer.closed.getCount()==1);
        producer.hold=true;AtomicReference<Exception> closeError=new AtomicReference<>();Thread closing=new Thread(()->{try{first.close();}catch(Exception error){closeError.set(error);}});closing.start();require(producer.closed.await(2,TimeUnit.SECONDS));require(closing.isAlive());rejected(()->pool.subscribe("camera","720p",()->producer));producer.finish.countDown();closing.join(2000);require(!closing.isAlive()&&closeError.get()==null);
        CaptureFanout<Packet> audio=new CaptureFanout<>(8,p->true);Producer mic=new Producer();var a=audio.subscribe("mic","pcm",()->mic);var other=audio.subscribe("mic","pcm",()->{throw new AssertionError();});mic.packets.put(new Packet(1,true));require(take(a).value()==1&&take(other).value()==1);a.close();require(mic.closed.getCount()==1);mic.packets.put(new Packet(2,false));require(take(other).value()==2);other.close();require(mic.closed.await(2,TimeUnit.SECONDS));
        CaptureFanout<Packet> mutePool=new CaptureFanout<>(8,p->true);Producer muting=new Producer();AtomicBoolean muteState=new AtomicBoolean(true);var muteDestination=mutePool.subscribe("mic","pcm",()->muting,p->new Packet(p.value(),muteState.get()));muting.packets.put(new Packet(1,false));Packet capturedMuted=take(muteDestination);muteState.set(false);require(capturedMuted.key());muteDestination.close();
        CaptureFanout<Packet> damaged=new CaptureFanout<>(3,p->true);Producer broken=new Producer();broken.failClose=true;var bad=damaged.subscribe("broken","x",()->broken);require(broken.started.await(2,TimeUnit.SECONDS));rejected(bad::close);require(broken.closed.await(2,TimeUnit.SECONDS));rejected(()->damaged.subscribe("broken","x",()->broken));
        CaptureFanout<Packet> failurePool=new CaptureFanout<>(3,p->true);Producer failing=new Producer();var failedA=failurePool.subscribe("source","x",()->failing);var failedB=failurePool.subscribe("source","x",()->failing);failing.packets.put(new Packet(-1,false));rejected(()->take(failedA));rejected(()->take(failedB));require(failing.closed.await(2,TimeUnit.SECONDS));
        Producer restarted=new Producer();CaptureFanout<Packet>.Subscription replacement=null;for(int i=0;i<100&&replacement==null;i++){try{replacement=failurePool.subscribe("source","x",()->restarted);}catch(IOException cleanupPending){Thread.sleep(5);}}require(replacement!=null);rejected(failedA::close);rejected(failedB::close);restarted.packets.put(new Packet(9,true));require(take(replacement).value()==9);replacement.close();require(restarted.closed.await(2,TimeUnit.SECONDS));
        require(FanoutPackets.microphoneCaptureStart(1_000_000_000L,4800,0,0,0,false)==890_000_000L);
        require(FanoutPackets.microphoneCaptureStart(1_000_000_000L,4800,0,4800,900_000_000L,true)==800_000_000L);
        require(FanoutPackets.microphoneCaptureStart(1_000_000_000L,4800,0,0,1_100_000_000L,true)==890_000_000L);
        // Native producer backlog and a dequeued partial packet must remain
        // silent after unmute; a different recipient must remain unaffected.
        FanoutPackets.MicrophoneMute gate=new FanoutPackets.MicrophoneMute(),independent=new FanoutPackets.MicrophoneMute();
        require(gate.set(true,100));require(gate.silent(150));require(gate.set(false,200));
        require(gate.silent(150)&&gate.silent(200)&&!gate.silent(201)&&!independent.silent(150));
        require(gate.set(true,300));require(gate.set(false,400));require(gate.silent(250)&&!gate.silent(401));
        CaptureFanout<Packet> backlogPool=new CaptureFanout<>(8,p->true);Producer backlog=new Producer();CountDownLatch beforeFanout=new CountDownLatch(1),resumeFanout=new CountDownLatch(1);
        CaptureFanout.Producer<Packet> pausedProducer=new CaptureFanout.Producer<>(){public void start(){beforeFanout.countDown();}public Packet next()throws Exception{resumeFanout.await();return backlog.next();}public void close(){}};
        gate.set(true,500);var delayed=backlogPool.subscribe("backlog","pcm",()->pausedProducer,p->new Packet(p.value(),gate.silent(p.value())));
        require(beforeFanout.await(2,TimeUnit.SECONDS));backlog.packets.put(new Packet(550,false));gate.set(false,600);delayed.clearQueue();resumeFanout.countDown();require(take(delayed).key());delayed.close();
        FanoutPackets.MicrophoneTimeline timeline=new FanoutPackets.MicrophoneTimeline();var initial=timeline.next(96000,0);require(initial.sample()==0&&initial.discontinuity());var continuous=timeline.next(96480,0);require(continuous.sample()==480&&!continuous.discontinuity());var dropped=timeline.next(97920,0);require(dropped.sample()==1920&&dropped.discontinuity());require(timeline.next(98400,1).discontinuity());require(!timeline.next(98880,1).discontinuity());
        byte[] pcm={1,2,3,4};byte[] muted=FanoutPackets.microphoneBytes(pcm,true),plain=FanoutPackets.microphoneBytes(pcm,false);require(muted[0]==0&&plain[0]==1&&pcm[0]==1);plain[0]=9;require(pcm[0]==1);
        require(CameraPackets.independent(new byte[]{0,0,0,1,0x67,1,0,0,1,0x68,1,0,0,1,0x65,1}));
        require(!CameraPackets.independent(new byte[]{0,0,1,0x65,1}));require(!CameraPackets.independent(new byte[]{0,0,1,0x67,1,0,0,1,0x68,1,0,0,1,0x41,1}));
        System.out.println("Android shared capture fanout checks passed");
    }
}
