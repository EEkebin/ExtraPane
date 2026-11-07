mod pcm;
mod backend;
use std::sync::{Arc,mpsc};
use std::time::Duration;
use usbip_audio::{Server,MonotonicClock};
use usbip_audio::uac2::{Uac2Config,Uac2Device};
fn validate_slot(slot:u32)->Result<(),String>{if slot>0{Ok(())}else{Err("Microphone endpoint identity must be positive".into())}}
fn lease_name(slot:u32)->Result<String,String>{validate_slot(slot)?;Ok(format!("Global\\ExtraPane.MicrophoneEndpoint.{slot}"))}

#[cfg(windows)]struct Lease(*mut std::ffi::c_void);
#[cfg(windows)]mod lease_api{
    use std::ffi::c_void;
    #[link(name="kernel32")]extern "system" {pub fn CreateMutexW(attributes:*mut c_void,owner:i32,name:*const u16)->*mut c_void;pub fn GetLastError()->u32;pub fn CloseHandle(handle:*mut c_void)->i32;}
}
#[cfg(windows)]impl Lease{fn acquire(slot:u32)->Result<Self,String>{let name:Vec<u16>=lease_name(slot)?.encode_utf16().chain(Some(0)).collect();let h=unsafe{lease_api::CreateMutexW(std::ptr::null_mut(),0,name.as_ptr())};if h.is_null(){return Err("Cannot acquire microphone slot".into());}if unsafe{lease_api::GetLastError()}==183{unsafe{lease_api::CloseHandle(h);}return Err("Microphone slot is already in use".into());}Ok(Self(h))}}
#[cfg(windows)]impl Drop for Lease{fn drop(&mut self){unsafe{lease_api::CloseHandle(self.0);}}}
#[cfg(not(windows))]struct Lease;
#[cfg(not(windows))]impl Lease{fn acquire(_:u32)->Result<Self,String>{Err("Windows is required".into())}}

fn configuration(slot:u32)->Result<Uac2Config,String>{
    validate_slot(slot)?;
    Ok(Uac2Config{vendor_id:0x1209,product_id:0x0001,bcd_device:0x0100,manufacturer:"ExtraPane".into(),product:format!("ExtraPane Microphone {slot}"),serial:format!("extrapane-microphone-{slot}"),sample_rate:48000,playback_channels:0,capture_channels:1,playback_name:None,capture_name:Some(format!("ExtraPane Microphone {slot}")),capture_connector:false})
}
fn run(slot:u32)->Result<(),String>{
    let _lease=Lease::acquire(slot)?;
    let exe=backend::installed()?;
    let busid=format!("ep-{}",backend::serial()?);
    let ring=Arc::new(pcm::Ring::default());
    let device=Arc::new(Uac2Device::new(configuration(slot)?,ring.clone())?);
    let server=Server::start(0,Arc::new(MonotonicClock::new())).map_err(|e|e.to_string())?;
    server.add(&busid,device);
    let mut attached=match backend::Attachment::attach(exe,server.port(),&busid){Ok(attached)=>attached,Err(e)=>{server.remove(&busid);return Err(e);}};
    println!("MICROPHONE_READY slot={slot}");
    let (tx,rx)=mpsc::sync_channel(4);
    // A stalled/malicious local producer cannot retain the endpoint: the owner
    // times out independently even if this process-lifetime reader is blocked.
    std::thread::spawn(move||{let input=std::io::stdin();let mut reader=input.lock();let mut seq=0;loop{let packet=pcm::read_packet(&mut reader,&mut seq).map_err(|e|e.to_string());let end=matches!(packet,Ok(pcm::Packet::Stop)|Err(_));if tx.send(packet).is_err()||end{return;}}});
    let result=loop{match rx.recv_timeout(Duration::from_secs(5)){
        Ok(Ok(pcm::Packet::Stop))=>break Ok(()),
        Ok(Ok(packet))=>ring.accept(packet),
        Ok(Err(e))=>break Err(format!("Microphone input ended: {e}")),
        Err(_)=>break Err("Microphone input timed out or closed".into()),
    }};
    ring.accept(pcm::Packet::Stop);
    // Detach only the exact port returned for our attach; never --all or an
    // existing device discovered by a global list. Remove export on every path.
    let cleanup=attached.detach();server.remove(&busid);drop(server);
    cleanup?;println!("MICROPHONE_STOPPED");result
}
fn main(){
    let args:Vec<String>=std::env::args().skip(1).collect();
    let result=match args.as_slice(){
        [arg] if arg=="--check"=>pcm::check().and_then(|_|{for slot in [1,4,5,255,65536,u32::MAX]{configuration(slot)?.validate()?;}Ok(())}).map(|_|println!("MICROPHONE_CHECK_OK")),
        [arg] if arg=="--check-backend"=>backend::installed().map(|_|println!("MICROPHONE_BACKEND_OK")),
        [arg] if arg=="--help"=>{println!("ExtraPane.MicrophoneEndpoint.exe --run --slot <positive-id>\n--check performs isolated framing/format checks only.\nInput: EPMI v1 PCM16LE mono48k packets, mute, stop. Requires the managed signed USB/IP backend.");Ok(())},
        [run_arg,slot_arg,slot] if run_arg=="--run"&&slot_arg=="--slot"=>match slot.parse::<u32>(){Ok(slot) if validate_slot(slot).is_ok()=>run(slot),_=>Err("Microphone endpoint identity must be positive".into())},
        _=>Err("Use --run --slot <positive-id>, --check or --help".into()),
    };
    if let Err(error)=result{eprintln!("error: {error}");std::process::exit(1);}
}

#[cfg(test)]mod slot_tests{
    use super::*;
    use std::collections::HashSet;
    #[test]fn distinct_receiving_endpoint_identities(){
        let(mut names,mut serials,mut leases)=(HashSet::new(),HashSet::new(),HashSet::new());
        for slot in [1,4,5,255,65536,u32::MAX]{
            let config=configuration(slot).unwrap();config.validate().unwrap();
            assert_eq!(config.capture_name.as_deref(),Some(config.product.as_str()));
            assert!(names.insert(config.product));assert!(serials.insert(config.serial));
            let lease=lease_name(slot).unwrap();assert!(lease.starts_with("Global\\ExtraPane.MicrophoneEndpoint."));assert!(leases.insert(lease));
        }
        assert_eq!(names.len(),6);assert_eq!(serials.len(),6);assert_eq!(leases.len(),6);
        for slot in [0]{assert!(configuration(slot).is_err());assert!(lease_name(slot).is_err());}
    }
}
