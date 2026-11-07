use std::collections::VecDeque;
use std::io::{self, Read};
use std::sync::Mutex;
use usbip_audio::uac2::Uac2Streams;

pub const FRAMES: usize = 480;
pub const PCM_BYTES: usize = FRAMES * 2;
const CAPACITY: usize = FRAMES * 6; // 60 ms; oldest audio is dropped to bound latency.

#[derive(Debug, PartialEq)]
pub enum Packet { Pcm([u8; PCM_BYTES]), Mute(bool), Stop }
fn invalid(message: &str) -> io::Error { io::Error::new(io::ErrorKind::InvalidData, message) }
pub fn read_packet(reader: &mut impl Read, expected: &mut u64) -> io::Result<Packet> {
    let mut header = [0u8; 16];
    reader.read_exact(&mut header)?;
    if &header[..4] != b"EPMI" || header[4] != 1 || header[6..8] != [0, 0] {
        return Err(invalid("Unsupported microphone packet header"));
    }
    let size = u32::from_be_bytes(header[8..12].try_into().unwrap()) as usize;
    let seq = u32::from_be_bytes(header[12..16].try_into().unwrap()) as u64;
    if seq != *expected { return Err(invalid("Microphone packet sequence mismatch")); }
    let packet = match (header[5], size) {
        (1, PCM_BYTES) => { let mut pcm = [0; PCM_BYTES]; reader.read_exact(&mut pcm)?; Packet::Pcm(pcm) }
        (2, 1) => { let mut value = [0]; reader.read_exact(&mut value)?; match value[0] { 0 => Packet::Mute(false), 1 => Packet::Mute(true), _ => return Err(invalid("Invalid mute state")) } }
        (3, 0) => Packet::Stop,
        _ => return Err(invalid("Invalid microphone packet type or length")),
    };
    *expected += 1; // u64 deliberately rejects the first u32 wrap rather than accepting replay.
    Ok(packet)
}
#[derive(Default)]
struct State { samples: VecDeque<i32>, muted: bool, dropped: u64, underrun: u64 }
#[derive(Default)]
pub struct Ring { state: Mutex<State> }
impl Ring {
    pub fn accept(&self, packet: Packet) {
        let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
        match packet {
            Packet::Pcm(bytes) if !state.muted => {
                while state.samples.len() + FRAMES > CAPACITY { state.samples.pop_front(); state.dropped += 1; }
                for pair in bytes.chunks_exact(2) {
                    // UAC2 uses signed, left-aligned 24-in-32. Preserve the PCM16 amplitude.
                    state.samples.push_back(i16::from_le_bytes([pair[0], pair[1]]) as i32 * 65536);
                }
            }
            Packet::Mute(muted) => { state.muted = muted; state.samples.clear(); }
            Packet::Stop => { state.muted = true; state.samples.clear(); }
            _ => {}
        }
    }
}
impl Uac2Streams for Ring {
    fn playback(&self, _: u64, _: &[i32]) {} // Configuration exposes no playback endpoint.
    fn capture(&self, _: u64, output: &mut [i32]) {
        output.fill(0);
        let Ok(mut state) = self.state.try_lock() else { return }; // Never block the USB audio thread.
        if state.muted { return; }
        for value in output {
            if let Some(sample) = state.samples.pop_front() { *value = sample; }
            else { state.underrun += 1; }
        }
    }
}
pub fn check() -> Result<(), String> {
    let mut input = Vec::from(*b"EPMI\x01\x03\0\0\0\0\0\0\0\0\0\0");
    if read_packet(&mut input.as_slice(), &mut 0).map_err(|e| e.to_string())? != Packet::Stop { return Err("Packet check failed".into()); }
    input[8] = 0xff;
    if read_packet(&mut input.as_slice(), &mut 0).is_ok() { return Err("Length check failed".into()); }
    let ring = Ring::default(); let mut output = [1; 8]; ring.capture(0, &mut output);
    if output != [0; 8] { return Err("Silence check failed".into()); }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    fn frame(kind:u8, seq:u32, payload:&[u8]) -> Vec<u8> {
        let mut result=Vec::from(*b"EPMI\x01\0\0\0");result[5]=kind;
        result.extend_from_slice(&(payload.len() as u32).to_be_bytes());result.extend_from_slice(&seq.to_be_bytes());result.extend_from_slice(payload);result
    }
    #[test] fn framing_bounds_and_sequence() {
        let mut seq=0;let bytes=frame(1,0,&[0;PCM_BYTES]);assert!(matches!(read_packet(&mut bytes.as_slice(),&mut seq),Ok(Packet::Pcm(_))));assert_eq!(seq,1);
        assert!(read_packet(&mut bytes.as_slice(),&mut seq).is_err());
        for payload in [&[][..], &[0; PCM_BYTES+1][..]] { assert!(read_packet(&mut frame(1,0,payload).as_slice(),&mut 0).is_err()); }
        assert!(read_packet(&mut frame(2,0,&[2]).as_slice(),&mut 0).is_err());
        assert!(read_packet(&mut frame(3,0,&[])[..15].as_ref(),&mut 0).is_err());
    }
    #[test] fn ring_conversion_mute_and_underrun() {
        let ring=Ring::default();let mut bytes=[0;PCM_BYTES];bytes[..6].copy_from_slice(&[0,128,255,127,1,0]);ring.accept(Packet::Pcm(bytes));
        let mut out=[0;3];ring.capture(0,&mut out);assert_eq!(out,[i32::MIN,2147418112,65536]);
        ring.accept(Packet::Mute(true));ring.accept(Packet::Pcm(bytes));ring.accept(Packet::Mute(false));ring.capture(3,&mut out);assert_eq!(out,[0;3]);
        for _ in 0..20 { ring.accept(Packet::Pcm(bytes)); }assert_eq!(ring.state.lock().unwrap().samples.len(),CAPACITY);
        ring.accept(Packet::Stop);ring.capture(0,&mut out);assert_eq!(out,[0;3]);
    }
    #[test] fn sequence_never_wraps() {assert!(read_packet(&mut frame(3,0,&[]).as_slice(),&mut (u32::MAX as u64+1)).is_err());}
}
