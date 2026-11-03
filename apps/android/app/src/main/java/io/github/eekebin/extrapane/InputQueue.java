package io.github.eekebin.extrapane;

import java.util.ArrayDeque;

final class InputQueue {
    private final ArrayDeque<int[]> events=new ArrayDeque<>();
    private int caps;
    private boolean overflow;
    synchronized void grant(int value){caps=value;events.clear();overflow=false;}
    synchronized int capabilities(){return caps;}
    synchronized void add(int[] e){
        int cap=e[0]==0?0:1<<(e[0]-1);
        if(caps==0 || (cap&~caps)!=0 || overflow)return;
        int[] last=events.peekLast();
        if(last!=null && e[1]==0 && last[1]==0 && e[0]==last[0] && e[3]==last[3] && (e[0]==1 || e[0]==3))events.removeLast();
        if(events.size()>=128){overflow=true;events.clear();return;}
        events.addLast(e);
    }
    synchronized void reset(){events.clear();if(caps!=0)events.add(new int[15]);}
    synchronized int[] take(){
        if(overflow)throw new IllegalStateException("Input queue overflow; disconnecting to release input");
        int[] batch=new int[events.size()*15];int i=0;
        for(int[] event:events){System.arraycopy(event,0,batch,i,15);i+=15;}events.clear();return batch;
    }
}
