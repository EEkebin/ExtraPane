#pragma once
namespace extrapane::usb_host_setup {
// A present marker means verification failures must propagate, never fall through
// to a mutating repair. The backend exposes only read-only verification here;
// recovery records/bindings deliberately are not required to be empty for reuse.
template<class Verification>
bool reuse_exact_backend(Verification& verification){
    if(!verification.has_manifest())return false;
    verification.payload();
    verification.ownership();
    verification.registration();
    verification.monitor();
    return true;
}
}
