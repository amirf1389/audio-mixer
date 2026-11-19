// A notification from the operating system (Windows: the corner of the screen and the notification centre), shown also while the
// console is minimised or behind another program. Used for "a new version is ready". A click on it calls onClick (the console comes
// to the front on SOFTWARE UPDATE). Where the system has none (the other platforms for now) show() returns false and nothing happens.
#pragma once
#include <QString>
#include <functional>

namespace sysnotify {
bool show(const QString& title, const QString& text, std::function<void()> onClick);
void remove();   // takes the console's icon out of the notification area again (also at exit)
}
