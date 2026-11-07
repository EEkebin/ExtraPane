use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::io::Read;
use std::time::{Duration, Instant};

pub fn parse_port(bytes: &[u8]) -> Result<u32, String> {
    let text=std::str::from_utf8(bytes).map_err(|_| "Invalid attach response")?.trim();
    if text.is_empty() || text.len()>5 || !text.bytes().all(|b|b.is_ascii_digit()) {return Err("Attach returned no unambiguous owned port".into());}
    let port=text.parse::<u32>().map_err(|_|"Invalid port")?;
    if port>65535 {return Err("Invalid port".into());}Ok(port)
}
fn bounded(mut reader: impl Read) -> Result<Vec<u8>, String> {
    let mut bytes=Vec::new();reader.by_ref().take(65537).read_to_end(&mut bytes).map_err(|e|e.to_string())?;
    if bytes.len()>65536 {Err("USB/IP command output exceeded limit".into())}else{Ok(bytes)}
}
pub fn command(exe:&Path,args:&[&str])->Result<Vec<u8>,String>{
    let mut cmd=Command::new(exe);cmd.args(args).current_dir(exe.parent().ok_or("Missing executable folder")?).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    #[cfg(windows)] {use std::os::windows::process::CommandExt;cmd.creation_flags(0x08000000);}
    let mut child=cmd.spawn().map_err(|e|format!("USB/IP command could not start: {e}"))?;
    let stdout=child.stdout.take().ok_or("Missing command output")?;let stderr=child.stderr.take().ok_or("Missing command error output")?;
    let out=std::thread::spawn(move||bounded(stdout));let err=std::thread::spawn(move||bounded(stderr));
    let start=Instant::now();
    let status=loop {match child.try_wait(){Ok(Some(status))=>break Ok(status),Ok(None) if start.elapsed()<Duration::from_secs(10)=>std::thread::sleep(Duration::from_millis(25)),_=>{let _=child.kill();let _=child.wait();break Err("USB/IP command failed or timed out".to_string());}}};
    let output=out.join().map_err(|_|"Command output reader failed")??;let _errors=err.join().map_err(|_|"Command error reader failed")??;
    if !status?.success(){return Err("USB/IP command failed; check the managed USB/IP driver installation".into());}Ok(output)
}
pub struct Attachment { exe:PathBuf, port:Option<u32> }
impl Attachment {
    pub fn attach(exe:PathBuf,tcp_port:u16,busid:&str)->Result<Self,String>{
        let bytes=command(&exe,&["--tcp-port",&tcp_port.to_string(),"attach","--remote","127.0.0.1","--bus-id",busid,"--once","--terse"])?;
        Ok(Self{exe,port:Some(parse_port(&bytes)?)})
    }
    pub fn detach(&mut self)->Result<(),String>{
        if let Some(port)=self.port.take(){command(&self.exe,&["detach","--port",&port.to_string()])?;}Ok(())
    }
}
impl Drop for Attachment {fn drop(&mut self){if let Err(e)=self.detach(){eprintln!("MICROPHONE_CLEANUP_ERROR {e}");}}}
#[cfg(windows)]
mod trust {
    use super::*;
    use std::ffi::c_void;
    use std::os::windows::ffi::{OsStrExt,OsStringExt};
    use std::os::windows::fs::MetadataExt;
    #[repr(C)] struct Guid {a:u32,b:u16,c:u16,d:[u8;8]}
    #[repr(C)] struct File {size:u32,path:*const u16,handle:*mut c_void,subject:*mut Guid}
    #[repr(C)] struct Data {size:u32,policy:*mut c_void,sip:*mut c_void,ui:u32,revocation:u32,choice:u32,file:*mut File,state_action:u32,state:*mut c_void,url:*const u16,flags:u32,context:u32,signature:*mut c_void}
    #[link(name="shell32")] extern "system" {fn SHGetKnownFolderPath(id:*const Guid,flags:u32,token:*mut c_void,path:*mut *mut u16)->i32;}
    #[link(name="ole32")] extern "system" {fn CoTaskMemFree(ptr:*mut c_void);}
    #[link(name="wintrust")] extern "system" {fn WinVerifyTrust(window:*mut c_void,action:*const Guid,data:*mut Data)->i32;}
    #[link(name="bcrypt")] extern "system" {fn BCryptGenRandom(algorithm:*mut c_void,bytes:*mut u8,count:u32,flags:u32)->i32;}
    pub fn serial()->Result<String,String>{let mut bytes=[0;12];if unsafe{BCryptGenRandom(std::ptr::null_mut(),bytes.as_mut_ptr(),12,2)}<0{return Err("Cannot generate endpoint identity".into());}Ok(bytes.iter().map(|b|format!("{b:02x}")).collect())}
    pub fn installed()->Result<PathBuf,String>{
        // FOLDERID_ProgramFiles, from the OS shell; never caller environment, PATH or argv.
        let id=Guid{a:0x905e63b6,b:0xc1bf,c:0x494e,d:[0xb2,0x9c,0x65,0xb7,0x32,0xd3,0xd2,0x1a]};let mut ptr=std::ptr::null_mut();
        let hr=unsafe{SHGetKnownFolderPath(&id,0,std::ptr::null_mut(),&mut ptr)};if hr<0||ptr.is_null(){return Err("Cannot locate protected program files".into());}
        let mut len=0;unsafe{while len<32768&&*ptr.add(len)!=0{len+=1;}}
        if len==32768{unsafe{CoTaskMemFree(ptr.cast());}return Err("Invalid program files path".into());}
        let root=PathBuf::from(std::ffi::OsString::from_wide(unsafe{std::slice::from_raw_parts(ptr,len)}));unsafe{CoTaskMemFree(ptr.cast());}
        let mut path=root;for piece in ["ExtraPane Components","Microphone","usbip.exe"]{path.push(piece);let meta=std::fs::symlink_metadata(&path).map_err(|_|"Install the managed ExtraPane microphone component first")?;if meta.file_attributes()&0x400!=0{return Err("Microphone component cannot use reparse points".into());}}
        if !path.is_file(){return Err("Microphone component is missing".into());}
        let wide:Vec<u16>=path.as_os_str().encode_wide().chain(Some(0)).collect();
        let mut file=File{size:std::mem::size_of::<File>()as u32,path:wide.as_ptr(),handle:std::ptr::null_mut(),subject:std::ptr::null_mut()};
        let action=Guid{a:0x00aac56b,b:0xcd44,c:0x11d0,d:[0x8c,0xc2,0,0xc0,0x4f,0xc2,0x95,0xee]};
        let mut data=Data{size:std::mem::size_of::<Data>()as u32,policy:std::ptr::null_mut(),sip:std::ptr::null_mut(),ui:2,revocation:0,choice:1,file:&mut file,state_action:1,state:std::ptr::null_mut(),url:std::ptr::null(),flags:0x1010,context:0,signature:std::ptr::null_mut()};
        let result=unsafe{WinVerifyTrust(-1isize as *mut c_void,&action,&mut data)};data.state_action=2;unsafe{WinVerifyTrust(-1isize as *mut c_void,&action,&mut data);}
        if result!=0{return Err("Installed USB/IP client has no trusted Authenticode signature".into());}Ok(path)
    }
}
#[cfg(windows)]pub use trust::{installed,serial};
#[cfg(not(windows))]pub fn installed()->Result<PathBuf,String>{Err("Windows microphone endpoint requires Windows".into())}
#[cfg(not(windows))]pub fn serial()->Result<String,String>{Err("Windows microphone endpoint requires Windows".into())}
#[cfg(test)]mod tests {use super::*;#[test]fn exact_owned_port_only(){assert_eq!(parse_port(b"12\r\n").unwrap(),12);for text in [b"".as_slice(),b"1\n2",b"Port 1",b"65536",b"-1"]{assert!(parse_port(text).is_err());}}}
