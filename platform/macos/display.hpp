#pragma once
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <string>
namespace dm::mac {
class VirtualDisplay {
    NSTask* process_ = nil;
    NSPipe* control_ = nil;
    CGDirectDisplayID id_ = 0;
    void close() noexcept;

public:
    static bool available();
    VirtualDisplay(unsigned width, unsigned height, unsigned hz, unsigned slot);
    ~VirtualDisplay();
    VirtualDisplay(const VirtualDisplay&) = delete;
    CGDirectDisplayID displayID() const { return id_; }
};
int displayCheck();
int displayOwner(unsigned width, unsigned height, unsigned hz, unsigned slot);
bool displayOnline(CGDirectDisplayID display);
int displayList();
}
