package io.github.eekebin.extrapane;

import java.io.*;

/** Native shared PAKE and authenticated records; used only by the session thread. */
final class AccessChannel implements AutoCloseable {
    static { System.loadLibrary("extrapane"); }
    private long handle;
    private static native long open(DataInputStream input,OutputStream output,String code) throws IOException;
    private static native byte[] receive(long handle,int count) throws IOException;
    private static native void send(long handle,byte[] bytes) throws IOException;
    private static native void destroy(long handle);
    AccessChannel(DataInputStream input,OutputStream output,String code)throws IOException {handle=open(input,output,code);if(handle==0)throw new IOException("Access authentication failed");}
    final InputStream input=new InputStream(){
        public int read()throws IOException{return receive(handle,1)[0]&255;}
        public int read(byte[] b,int offset,int length)throws IOException{
            if(offset<0||length<0||offset>b.length-length)throw new IndexOutOfBoundsException();if(length==0)return 0;
            byte[] data=receive(handle,Math.min(length,65536));System.arraycopy(data,0,b,offset,data.length);return data.length;
        }
    };
    final OutputStream output=new OutputStream(){
        public void write(int b)throws IOException{send(handle,new byte[]{(byte)b});}
        public void write(byte[] b,int offset,int length)throws IOException{
            if(offset<0||length<0||offset>b.length-length)throw new IndexOutOfBoundsException();
            while(length>0){int count=Math.min(length,65536);send(handle,java.util.Arrays.copyOfRange(b,offset,offset+count));offset+=count;length-=count;}
        }
    };
    public void close(){if(handle!=0){destroy(handle);handle=0;}}
}
