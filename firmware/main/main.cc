#include "app/buddy_app.h"

extern "C" void app_main() {
    static BuddyApp app;
    if (!app.Init()) return;
    app.Run();
}
