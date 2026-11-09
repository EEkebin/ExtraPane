use std::collections::VecDeque;
use std::io::{self, Write};
use std::sync::{Arc, Mutex, mpsc};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};
use usbip_audio::{MonotonicClock, Server};
use usbip_audio::uac2::{Uac2Config, Uac2Device, Uac2Streams};
use crate::{backend, pcm, Lease, validate_slot};

const FRAMES: usize = 480;
const CHANNELS: usize = 2;
const SAMPLES: usize = FRAMES * CHANNELS;
const PREFILL: usize = SAMPLES * 2; // 20 ms USB-to-packetizer reserve.
const CAPACITY: usize = SAMPLES * 12; // 120 ms; never retain an unbounded output backlog.
const HEADER: usize = 24;
const PACKET_BYTES: usize = HEADER + SAMPLES * 2;
const PERIOD: Duration = Duration::from_millis(10);

fn bus_id(serial:&str)->Result<String,String>{
    if serial.len()!=24||!serial.bytes().all(|b|b.is_ascii_hexdigit()){return Err("Invalid speaker session identity".into());}
    // USB/IP's bus-id field is 32 bytes including its terminating zero.
    Ok(format!("eps-{serial}"))
}
fn lease_name(slot:u32)->Result<String,String>{validate_slot(slot)?;Ok(format!("Global\\ExtraPane.SpeakerEndpoint.{slot}"))}
fn configuration(slot:u32)->Result<Uac2Config,String>{
    validate_slot(slot)?;
    Ok(Uac2Config{vendor_id:0x1209,product_id:0x0001,bcd_device:0x0100,
        manufacturer:"ExtraPane".into(),product:format!("ExtraPane Speakers {slot}"),
        serial:format!("extrapane-speakers-{slot}"),sample_rate:48000,
        playback_channels:2,capture_channels:0,playback_name:Some(format!("ExtraPane Speakers {slot}")),
        capture_name:None,capture_connector:false})
}
struct State {
    samples:VecDeque<i16>, expected_frame:Option<u64>, muted:bool, primed:bool,
}
impl Default for State {
    fn default()->Self{Self{samples:VecDeque::with_capacity(CAPACITY),expected_frame:None,muted:false,primed:false}}
}
#[derive(Default)]
struct Render {
    state:Mutex<State>, discontinuity:AtomicBool,
}
impl Render {
    fn mute(&self,value:bool){
        let mut state=self.state.lock().unwrap_or_else(|e|e.into_inner());
        state.muted=value;state.samples.clear();state.expected_frame=None;state.primed=false;
        self.discontinuity.store(true,Ordering::Release);
    }
    fn reset(&self){
        let mut state=self.state.lock().unwrap_or_else(|e|e.into_inner());
        state.samples.clear();state.expected_frame=None;state.primed=false;
        self.discontinuity.store(true,Ordering::Release);
    }
    fn packet(&self,sequence:u64,position:u64)->[u8;PACKET_BYTES]{
        let mut result=[0;PACKET_BYTES];result[..4].copy_from_slice(b"EPSP");result[4]=1;
        result[8..16].copy_from_slice(&sequence.to_be_bytes());result[16..24].copy_from_slice(&position.to_be_bytes());
        let mut samples=[0i16;SAMPLES];
        let discontinuity={
            let mut state=self.state.lock().unwrap_or_else(|e|e.into_inner());
            if !state.muted&&!state.primed&&state.samples.len()>=PREFILL {state.primed=true;}
            let interrupted=state.primed&&state.samples.len()<SAMPLES;
            if interrupted {state.primed=false;}
            if !state.muted&&state.primed {
                // Transfer complete PCM blocks only. Keep a partial late block
                // until a reserve returns instead of padding every packet gap.
                for (out,value) in samples.iter_mut().zip(state.samples.drain(..SAMPLES)){*out=value;}
            }
            let pending=self.discontinuity.swap(false,Ordering::AcqRel);
            interrupted||pending
        };
        // Conversion and pipe writes run after releasing the USB callback lock.
        for (index,sample) in samples.iter().enumerate(){
            result[HEADER+index*2..HEADER+index*2+2].copy_from_slice(&sample.to_le_bytes());
        }
        if discontinuity{result[5]=1;}
        result
    }

}
impl Uac2Streams for Render {
    fn capture(&self,_:u64,output:&mut [i32]){output.fill(0);} // No capture endpoint exists.
    fn playback(&self,frame:u64,input:&[i32]){
        if input.is_empty(){return;}
        if input.len()%CHANNELS!=0{self.discontinuity.store(true,Ordering::Release);return;}
        // The USB callback never waits for a pipe, network, disk or another thread.
        let Ok(mut state)=self.state.try_lock() else {self.discontinuity.store(true,Ordering::Release);return;};
        if state.muted{return;}
        if state.expected_frame.is_some_and(|expected|expected!=frame){state.samples.clear();state.primed=false;self.discontinuity.store(true,Ordering::Release);}
        state.expected_frame=frame.checked_add((input.len()/CHANNELS) as u64);
        if state.expected_frame.is_none(){state.samples.clear();state.primed=false;self.discontinuity.store(true,Ordering::Release);return;}
        let input=if input.len()>CAPACITY {state.primed=false;self.discontinuity.store(true,Ordering::Release);&input[input.len()-CAPACITY..]}else{input};
        let excess=(state.samples.len()+input.len()).saturating_sub(CAPACITY);
        if excess>0{state.samples.drain(..excess);state.primed=false;self.discontinuity.store(true,Ordering::Release);}
        // UAC2 supplies signed 24-in-32, most significant bits valid. Keep amplitude and stereo order.
        for &value in input {state.samples.push_back((value>>16) as i16);}
    }
}
fn stream(render:Arc<Render>,stop:Arc<AtomicBool>,mut output:impl Write)->io::Result<()> {
    let(mut sequence,mut position)=(0u64,0u64);let mut next=Instant::now();
    while !stop.load(Ordering::Acquire){
        let now=Instant::now();
        if now<next {std::thread::sleep((next-now).min(Duration::from_millis(2)));continue;}
        if now.duration_since(next)>=PERIOD {
            // Don't send a catch-up burst after a scheduler/pipe stall. Discard stale audio.
            let missed=now.duration_since(next).as_nanos()/PERIOD.as_nanos();
            let skip=u64::try_from(missed).ok().and_then(|n|n.checked_mul(FRAMES as u64)).ok_or_else(||io::Error::other("Speaker timeline overflow"))?;
            position=position.checked_add(skip).ok_or_else(||io::Error::other("Speaker timeline overflow"))?;
            render.reset();next=now;
        }
        let packet=render.packet(sequence,position);output.write_all(&packet)?;output.flush()?;
        sequence=sequence.checked_add(1).ok_or_else(||io::Error::other("Speaker sequence exhausted"))?;
        position=position.checked_add(FRAMES as u64).ok_or_else(||io::Error::other("Speaker timeline overflow"))?;
        next+=PERIOD;
    }
    Ok(())
}
#[cfg(windows)]fn cancel_writer(writer:&std::thread::JoinHandle<()>){
    use std::os::windows::io::AsRawHandle;
    #[link(name="kernel32")]unsafe extern "system"{fn CancelSynchronousIo(thread:*mut std::ffi::c_void)->i32;}
    // A full parent pipe must not retain the endpoint after Stop/EOF.
    unsafe{CancelSynchronousIo(writer.as_raw_handle());}
}
#[cfg(not(windows))]fn cancel_writer(_:&std::thread::JoinHandle<()>){}
pub fn run(slot:u32)->Result<(),String>{
    let _lease=Lease::acquire_named(lease_name(slot)?)?;
    let render=Arc::new(Render::default());let stop=Arc::new(AtomicBool::new(false));
    let device=Arc::new(Uac2Device::new(configuration(slot)?,render.clone())?);
    let server=Server::start(0,Arc::new(MonotonicClock::new())).map_err(|e|e.to_string())?;
    let busid=bus_id(&backend::serial()?)?;server.add(&busid,device);
    let mut attached=backend::Attachment::attach(backend::installed()?,server.port(),&busid)?;
    eprintln!("SPEAKER_READY slot={slot}");
    let (tx,rx)=mpsc::sync_channel(4);
    let controls=tx.clone();
    std::thread::spawn(move||{
        let input=io::stdin();let mut input=input.lock();let mut sequence=0;
        loop {
            let packet=pcm::read_packet(&mut input,&mut sequence).map_err(|e|e.to_string());
            let end=matches!(packet,Ok(pcm::Packet::Stop)|Err(_));
            if controls.send(packet).is_err()||end{return;}
        }
    });
    let writer_render=render.clone();let writer_stop=stop.clone();
    // stdout is binary only. Control/cleanup remains independent of pipe backpressure.
    let writer=std::thread::spawn(move||{
        let output=io::stdout();let result=stream(writer_render,writer_stop,output.lock());
        let _=tx.try_send(Err(result.err().map_or_else(||"Speaker writer stopped".into(),|e|format!("Speaker output ended: {e}"))));
    });
    let result=loop{match rx.recv_timeout(Duration::from_millis(50)){
        Ok(Ok(pcm::Packet::Stop))=>break Ok(()),
        Ok(Ok(pcm::Packet::Mute(value)))=>render.mute(value),
        Ok(Ok(_))=>break Err("Speaker input accepts only EPMI mute and stop commands".into()),
        Ok(Err(error))=>break Err(error),
        Err(mpsc::RecvTimeoutError::Timeout) if !writer.is_finished()=>continue,
        Err(_)=>break Err("Speaker control or writer ended".into()),
    }};
    stop.store(true,Ordering::Release);render.mute(true);
    let until=Instant::now()+Duration::from_secs(1);
    while !writer.is_finished()&&Instant::now()<until{cancel_writer(&writer);std::thread::sleep(Duration::from_millis(5));}
    if writer.is_finished(){let _=writer.join();}
    let cleanup=attached.detach();server.remove(&busid);drop(server);
    cleanup?;eprintln!("SPEAKER_STOPPED");result
}
pub fn check()->Result<(),String>{
    for slot in [1,4,255,u32::MAX]{configuration(slot)?.validate()?;}
    let packet=Render::default().packet(0,0);
    if &packet[..4]!=b"EPSP"||packet[4]!=1||packet[HEADER..].iter().any(|&v|v!=0){return Err("Speaker idle packet check failed".into());}
    Ok(())
}

#[cfg(test)]mod tests{
    use super::*;
    #[test]fn bus_identity_fits_usbip_field(){
        let id=bus_id("0123456789abcdef01234567").unwrap();assert!(id.len()<32);assert!(id.starts_with("eps-"));
        assert!(bus_id("").is_err());assert!(bus_id("0123456789abcdef0123456789abcdef").is_err());
        assert!(bus_id("0123456789abcdef0123456/").is_err());
    }
    #[test]fn playback_only_identity_is_distinct(){
        for slot in [1,7,u32::MAX]{let cfg=configuration(slot).unwrap();cfg.validate().unwrap();assert_eq!(cfg.playback_channels,2);assert_eq!(cfg.capture_channels,0);assert_ne!(lease_name(slot).unwrap(),crate::lease_name(slot).unwrap());assert_ne!(cfg.serial,crate::configuration(slot).unwrap().serial);}
        assert!(configuration(0).is_err());assert!(lease_name(0).is_err());
        assert!(bus_id("0123456789abcdef01234567").unwrap().len()<32);
    }
    #[test]fn stereo_amplitude_and_header(){
        let render=Render::default();let mut input=[0;SAMPLES];input[..4].copy_from_slice(&[i32::MIN,i32::MAX,65536,-65536]);render.playback(0,&input);render.playback(FRAMES as u64,&[0;SAMPLES]);
        let packet=render.packet(3,1440);assert_eq!(&packet[..8],b"EPSP\x01\0\0\0");assert_eq!(&packet[8..16],&3u64.to_be_bytes());assert_eq!(&packet[16..24],&1440u64.to_be_bytes());assert_eq!(&packet[24..32],&[0,128,255,127,1,0,255,255]);
    }
    #[test]fn bounded_overflow_and_mute_do_not_replay(){
        let render=Render::default();let input=vec![65536;CAPACITY+SAMPLES];render.playback(0,&input);assert_eq!(render.state.lock().unwrap().samples.len(),CAPACITY);
        assert_eq!(render.packet(0,0)[5],1);render.mute(true);render.playback(0,&[131072;SAMPLES]);render.mute(false);
        let packet=render.packet(1,480);assert_eq!(packet[5],1);assert!(packet[HEADER..].iter().all(|&v|v==0));
    }
    #[test]fn callback_contention_never_blocks_and_marks_gap(){
        let render=Render::default();let lock=render.state.lock().unwrap();render.playback(0,&[1;SAMPLES]);drop(lock);assert_eq!(render.packet(0,0)[5],1);
    }
    #[test]fn idle_and_underflow_wait_for_a_complete_reserve(){
        let render=Render::default();assert!(render.packet(0,0)[HEADER..].iter().all(|&v|v==0));
        render.playback(0,&[65536;SAMPLES]);
        assert!(render.packet(1,480)[HEADER..].iter().all(|&v|v==0));
        render.playback(480,&[65536;SAMPLES]);
        assert_eq!(&render.packet(2,960)[24..28],&[1,0,1,0]);
        render.packet(3,1440);
        let empty=render.packet(4,1920);assert_eq!(empty[5],1);assert!(empty[HEADER..].iter().all(|&v|v==0));
        render.playback(960,&[131072;SAMPLES]);
        assert!(render.packet(5,2400)[HEADER..].iter().all(|&v|v==0));
        render.playback(1440,&[131072;SAMPLES]);
        let resumed=render.packet(6,2880);assert_eq!(resumed[5],0);assert_eq!(&resumed[24..28],&[2,0,2,0]);
    }
    #[test]fn short_usb_arrival_is_retained_and_bursts_do_not_repeat_gaps(){
        let render=Render::default();render.playback(0,&[65536;SAMPLES]);render.playback(480,&[65536;SAMPLES]);
        render.packet(0,0);render.packet(1,480);
        render.playback(960,&[131072;SAMPLES/2]);
        let gap=render.packet(2,960);assert_eq!(gap[5],1);assert!(gap[HEADER..].iter().all(|&v|v==0));
        assert_eq!(render.state.lock().unwrap().samples.len(),SAMPLES/2);
        render.playback(1200,&[131072;SAMPLES+SAMPLES/2]);
        let resumed=render.packet(3,1440);assert_eq!(resumed[5],0);assert!(resumed[HEADER..].chunks_exact(2).all(|v|v==[2,0]));
        // 20 ms USB delivery bursts into a 10 ms output cadence for 10 seconds.
        let steady=Render::default();steady.playback(0,&[196608;PREFILL]);
        let mut position=960;
        for tick in 0..1000 {
            if tick>0&&tick%2==0 {steady.playback(position,&[196608;PREFILL]);position+=960;}
            let packet=steady.packet(tick,tick*480);assert_eq!(packet[5],0);
            assert!(packet[HEADER..].chunks_exact(2).all(|v|v==[3,0]));
        }
    }
    #[test]fn reset_and_other_slots_are_independent(){
        let first=Render::default();let second=Render::default();first.playback(0,&[65536;SAMPLES]);second.playback(0,&[131072;PREFILL]);first.reset();assert!(first.packet(0,0)[HEADER..].iter().all(|&v|v==0));assert_eq!(&second.packet(0,0)[24..28],&[2,0,2,0]);
    }
    #[test]fn paced_writer_sends_idle_packets_and_obeys_stop(){
        struct Sink{stop:Arc<AtomicBool>,bytes:Vec<u8>}
        impl Write for Sink{
            fn write(&mut self,bytes:&[u8])->io::Result<usize>{self.bytes.extend_from_slice(bytes);if self.bytes.len()==PACKET_BYTES*3{self.stop.store(true,Ordering::Release);}Ok(bytes.len())}
            fn flush(&mut self)->io::Result<()>{Ok(())}
        }
        let stop=Arc::new(AtomicBool::new(false));let mut sink=Sink{stop:stop.clone(),bytes:Vec::new()};
        stream(Arc::new(Render::default()),stop,&mut sink).unwrap();assert_eq!(sink.bytes.len(),PACKET_BYTES*3);
        for (index,packet) in sink.bytes.chunks_exact(PACKET_BYTES).enumerate(){assert_eq!(&packet[..4],b"EPSP");assert_eq!(u64::from_be_bytes(packet[8..16].try_into().unwrap()),index as u64);assert!(packet[HEADER..].iter().all(|&v|v==0));}
    }
    #[test]fn writer_propagates_broken_parent_pipe(){
        struct Closed;impl Write for Closed{fn write(&mut self,_:&[u8])->io::Result<usize>{Err(io::Error::new(io::ErrorKind::BrokenPipe,"parent gone"))}fn flush(&mut self)->io::Result<()>{Ok(())}}
        let error=stream(Arc::new(Render::default()),Arc::new(AtomicBool::new(false)),Closed).unwrap_err();assert_eq!(error.kind(),io::ErrorKind::BrokenPipe);
    }
}
