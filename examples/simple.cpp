#include "application.hpp"

int main() {
    vk::ApplicationInfo app_info{"simple", 1, "engine", 1, VK_API_VERSION_1_0};
    common::application app{app_info, 800, 600};

    app.run();
}
