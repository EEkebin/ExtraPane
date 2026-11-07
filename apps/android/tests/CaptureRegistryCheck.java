package io.github.eekebin.extrapane;
public final class CaptureRegistryCheck {
    private static void require(boolean value){if(!value)throw new AssertionError("Capture lifecycle");}
    private static void blocked(Runnable action){try{action.run();throw new AssertionError("Expected source ownership rejection");}catch(IllegalStateException expected){}}
    public static void main(String[] args){
        CaptureRegistry.Sources sources=new CaptureRegistry.Sources();CaptureRegistry service=new CaptureRegistry(sources),replacementService=new CaptureRegistry(sources);
        var camera=service.begin("camera-one","camera:0");var microphone=service.begin("mic-one","microphone:1");
        require(service.stop("camera-one").size()==1&&camera.stopping()&&!microphone.stopping());
        blocked(()->service.begin("camera-two","camera:0"));blocked(()->replacementService.begin("restored","camera:0"));
        service.finish(camera);require(!service.current(camera)&&service.current(microphone));
        var restarted=service.begin("camera-one","camera:0");require(service.current(restarted));
        service.finish(camera);require(service.current(restarted));blocked(()->replacementService.begin("restored","camera:0"));
        require(service.stop(null).size()==2&&restarted.stopping()&&microphone.stopping());
        service.finish(restarted);service.finish(microphone);require(service.stop(null).isEmpty());
        var successor=replacementService.begin("restored","camera:0");require(replacementService.current(successor));replacementService.finish(successor);
        var sharedA=service.beginShared("shared-a","camera:fanout");var sharedB=service.beginShared("shared-b","camera:fanout");service.finish(sharedA);blocked(()->service.begin("exclusive","camera:fanout"));require(service.current(sharedB));service.finish(sharedB);
        var first=DeviceCaptureLease.acquire("mic:1");var second=DeviceCaptureLease.acquire("mic:2");blocked(()->DeviceCaptureLease.acquire("mic:1"));
        second.close();blocked(()->DeviceCaptureLease.acquire("mic:1"));first.close();var newer=DeviceCaptureLease.acquire("mic:1");first.close();blocked(()->DeviceCaptureLease.acquire("mic:1"));newer.close();
        CaptureAttempt attempt=new CaptureAttempt();String old=attempt.begin();require(attempt.accept(old,false));String fresh=attempt.begin();require(!attempt.accept(old,false)&&attempt.current(fresh));
        require(attempt.accept(fresh,true));require(attempt.accept(fresh,false));require(!attempt.accept(fresh,false));
        String pending=attempt.begin();attempt.cancelPending();require(!attempt.current(pending)&&!attempt.accept(pending,false));
        System.out.println("Android capture ownership and generation checks passed");
    }
}
