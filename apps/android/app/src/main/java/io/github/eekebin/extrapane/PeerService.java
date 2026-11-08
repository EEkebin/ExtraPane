package io.github.eekebin.extrapane;

import java.io.File;
import java.io.IOException;

/** Metadata/control only. Native workers own TLS, authentication and media tunnels.
 * Native receiving devices and USB sharing are not available on Android.
 */
final class PeerService implements AutoCloseable {
    static { System.loadLibrary("extrapane"); }
    private long handle;
    PeerService(File directory,String name)throws IOException {handle=nativeCreate(directory.getAbsolutePath(),name);}
    private long alive()throws IOException {if(handle==0)throw new IOException("Device sharing has stopped");return handle;}
    synchronized String identity()throws IOException{return nativeIdentity(alive());}
    synchronized String[][] remembered()throws IOException{return nativeRemembered(alive());}
    synchronized String pairingCode()throws IOException{return nativeCode(alive());}
    /** Offer rows: id,label,kind,available,inputAllowed,width,height,refresh,reason. */
    synchronized void offers(String[][] rows)throws IOException{nativeOffers(alive(),rows);}
    synchronized void start(String address,int port)throws IOException{nativeStart(alive(),address,port);}
    synchronized void stop()throws IOException{nativeStop(alive());}
    synchronized void setPairingRequired(boolean value)throws IOException{nativePairing(alive(),value);}
    /** Going into background stops all sharing; no camera/mic starts behind UI. */
    synchronized void setForeground(boolean value)throws IOException{nativeForeground(alive(),value);}
    synchronized long connect(String address,int port,String local,String expectedHex,String code,boolean openMode)throws IOException{return nativeConnect(alive(),address,port,local,expectedHex,code,openMode);}
    synchronized long catalog(long connection)throws IOException{return nativeCatalog(alive(),connection);}
    synchronized long open(long connection,String owner,String resource,boolean input,int width,int height,int refresh)throws IOException{return nativeOpen(alive(),connection,owner,resource,input,width,height,refresh);}
    synchronized void closeResource(long connection,long grant)throws IOException{nativeCloseResource(alive(),connection,grant);}
    synchronized void disconnect(long connection)throws IOException{nativeDisconnect(alive(),connection);}
    synchronized void forget(String device)throws IOException{nativeForget(alive(),device);}
    /** Rows: type,connection,request,grant,peerHex,kind,port,detail,ownerHex,
     * resource,width,height,refresh,credential,input. Catalog rows additionally
     * contain available,unavailableReason,ownerName at columns15..17.
     * native-ready/stop are private adapter commands, not user-visible status.
     * Never log the credential column. Native-ready launches only a user-enabled
     * local source, using 127.0.0.1:port, after checking foreground permissions.
     */
    synchronized String[][] poll()throws IOException{return nativePoll(alive());}
    synchronized byte[] announcement(int port,long sequence)throws IOException{return nativeAnnouncement(alive(),port,sequence,false);}
    synchronized byte[] withdrawal(long sequence)throws IOException{return nativeAnnouncement(alive(),0,sequence,true);}
    // Decoded untrusted hints only. A withdrawal has lifetime "0"; never trust
    // the advertised identity until pairing authenticates the TLS connection.
    static String[] decodeAnnouncement(byte[] bytes)throws IOException{return nativeDecodeAnnouncement(bytes);}
    public synchronized void close(){if(handle!=0){long old=handle;handle=0;nativeDestroy(old);}}
    private static native byte[] nativeAnnouncement(long h,int port,long sequence,boolean withdraw)throws IOException;
    private static native String[] nativeDecodeAnnouncement(byte[] bytes)throws IOException;
    private static native long nativeCreate(String path,String name)throws IOException;
    private static native String nativeIdentity(long h)throws IOException;
    private static native String[][] nativeRemembered(long h)throws IOException;
    private static native String nativeCode(long h)throws IOException;
    private static native void nativeOffers(long h,String[][] rows)throws IOException;
    private static native void nativeStart(long h,String address,int port)throws IOException;
    private static native void nativeStop(long h)throws IOException;
    private static native void nativePairing(long h,boolean required)throws IOException;
    private static native void nativeForeground(long h,boolean foreground)throws IOException;
    private static native long nativeConnect(long h,String address,int port,String local,String expected,String code,boolean open)throws IOException;
    private static native long nativeCatalog(long h,long connection)throws IOException;
    private static native long nativeOpen(long h,long connection,String owner,String resource,boolean input,int width,int height,int refresh)throws IOException;
    private static native void nativeCloseResource(long h,long connection,long grant)throws IOException;
    private static native void nativeDisconnect(long h,long connection)throws IOException;
    private static native void nativeForget(long h,String device)throws IOException;
    private static native String[][] nativePoll(long h)throws IOException;
    private static native void nativeDestroy(long h);
}
