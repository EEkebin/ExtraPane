package io.github.eekebin.extrapane;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Predicate;
import java.util.function.UnaryOperator;

/** Native-source ownership with bounded, independent destination queues. */
final class CaptureFanout<T> {
    interface Producer<T> extends AutoCloseable {void start()throws Exception;T next()throws Exception;void close()throws Exception;}
    interface Factory<T> {Producer<T> create()throws Exception;}
    private final Map<String,Source> sources=new HashMap<>();
    private final int capacity;private final Predicate<T> joinable;
    CaptureFanout(int capacity,Predicate<T> joinable){if(capacity<1||capacity>8)throw new IllegalArgumentException("Invalid subscriber capacity");this.capacity=capacity;this.joinable=joinable;}
    final class Subscription implements AutoCloseable {
        private final Source source;private final UnaryOperator<T> map;private final ArrayBlockingQueue<T> queue=new ArrayBlockingQueue<>(capacity);
        private volatile IOException error;private volatile boolean closed;private boolean joined;private volatile long lastPacket=System.nanoTime();
        private Subscription(Source source,UnaryOperator<T> map){this.source=source;this.map=map;}
        T next()throws Exception{
            if(error!=null)throw error;if(closed)throw new IOException("Sharing stopped");
            T packet=queue.poll(100,TimeUnit.MILLISECONDS);
            if(error!=null)throw error;if(closed)throw new IOException("Sharing stopped");
            if(packet==null&&System.nanoTime()-lastPacket>15_000_000_000L){fail(this,new IOException("Source did not deliver a decodable frame or audio packet in time"));throw error;}
            return packet;
        }
        void clearQueue(){synchronized(sources){queue.clear();}}
        public void close()throws IOException{
            boolean last;synchronized(sources){if(closed)return;closed=true;queue.clear();source.subscribers.remove(this);last=source.subscribers.isEmpty();stopIfEmpty(source);}
            if(last){try{source.join(6000);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();throw new IOException("Interrupted source cleanup",interrupted);}
                synchronized(sources){if(source.isAlive())throw new IOException("Source cleanup is still pending");if(source.terminal!=null)throw source.terminal;}}
        }
    }
    private final class Source extends Thread {
        final String key,format;final Factory<T> factory;final Set<Subscription> subscribers=new LinkedHashSet<>();boolean stopping;IOException terminal;
        Source(String key,String format,Factory<T> factory){super("ExtraPane shared "+key);this.key=key;this.format=format;this.factory=factory;}
        public void run(){Producer<T> producer=null;Exception cleanup=null;
            try{synchronized(sources){if(stopping)return;}producer=factory.create();synchronized(sources){if(stopping)return;}producer.start();for(;;){synchronized(sources){if(stopping)break;}T packet=producer.next();if(packet==null)continue;
                synchronized(sources){if(stopping)break;boolean canJoin=joinable.test(packet);Iterator<Subscription> iterator=subscribers.iterator();
                    while(iterator.hasNext()){Subscription subscriber=iterator.next();if(!subscriber.joined&&!canJoin)continue;subscriber.joined=true;
                        if(!subscriber.queue.offer(subscriber.map.apply(packet))){subscriber.error=new IOException("This destination is too slow; its bounded media queue filled");subscriber.queue.clear();iterator.remove();}else subscriber.lastPacket=System.nanoTime();}
                    stopIfEmpty(this);
                }
            }}catch(Exception error){synchronized(sources){if(!stopping){terminal=new IOException(error.getMessage()==null?"Shared capture failed":error.getMessage(),error);for(Subscription subscriber:subscribers){subscriber.error=terminal;subscriber.queue.clear();}}stopping=true;}}
            finally{
                Thread.interrupted();if(producer!=null)try{producer.close();}catch(Exception error){cleanup=error;}
                synchronized(sources){
                    if(cleanup==null)sources.remove(key,this);
                    else terminal=new IOException("Source cleanup is still pending; restart the app before reusing this source",cleanup);
                    for(Subscription subscriber:subscribers){if(subscriber.error==null)subscriber.error=terminal==null?new IOException("Shared capture stopped"):terminal;subscriber.queue.clear();}subscribers.clear();
                }
            }
        }
    }
    Subscription subscribe(String key,String format,Factory<T> factory)throws IOException{return subscribe(key,format,factory,UnaryOperator.identity());}
    Subscription subscribe(String key,String format,Factory<T> factory,UnaryOperator<T> map)throws IOException{
        synchronized(sources){Source source=sources.get(key);boolean fresh=source==null;if(fresh){source=new Source(key,format,factory);sources.put(key,source);}else if(source.stopping)throw source.terminal==null?new IOException("Source is still stopping; retry shortly"):source.terminal;
            if(!source.format.equals(format))throw new IOException("This source is already sharing a different capture mode");Subscription subscriber=new Subscription(source,map);source.subscribers.add(subscriber);
            if(fresh)try{source.start();}catch(RuntimeException error){source.subscribers.clear();sources.remove(key,source);throw error;}return subscriber;}
    }
    private void fail(Subscription subscriber,IOException error){synchronized(sources){if(subscriber.error==null)subscriber.error=error;subscriber.queue.clear();subscriber.source.subscribers.remove(subscriber);stopIfEmpty(subscriber.source);}}
    private void stopIfEmpty(Source source){if(source.subscribers.isEmpty()&&!source.stopping){source.stopping=true;source.interrupt();}}
}
