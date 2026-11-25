// HIDE FROM SCREENSHOTS & SCREEN SHARING (macOS): NSWindow.sharingType = NSWindowSharingNone. Qt's winId() on macOS is the NSView.
#import <AppKit/AppKit.h>

bool amixMacSetSharingNone(void* nsview, bool on) {
  NSView* v = (__bridge NSView*)nsview;
  if (!v || !v.window) return false;
  v.window.sharingType = on ? NSWindowSharingNone : NSWindowSharingReadOnly;
  return true;
}
