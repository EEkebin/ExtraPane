#pragma once
#import <AppKit/AppKit.h>
#include "resource_session.hpp"
#include <functional>

// Isolated AppKit menu regression check; never enumerates or opens devices.
bool resourceSourceMenuCheck();

// AppKit adapter for the common resource protocol. Native media remains off UI.
@interface EPResourcePanel : NSObject
@property(nonatomic, readonly) NSView* view;
@property(nonatomic, readonly) BOOL active;
@property(nonatomic, copy) void (^highlightInvalidTarget)(void);
- (instancetype)initWithTarget:(std::function<dm::mac::ResourceOptions()>)target changed:(void (^)(void))changed;
- (void)stopAll;
@end
