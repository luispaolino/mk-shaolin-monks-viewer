#include "host.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "game_setup.h"
#include "runtime_paths.h"
#include "launcher.h"

bool app::FindGame(GameFiles& game) {
    bool remember = shaolin::options.register_disc;
    auto input = shaolin::options.disc.empty() ? shaolin::options.game : shaolin::options.disc;
    if (input.empty() && !shaolin::options.background) {
        if (!shaolin::options.setup) input = shaolin::setup::RememberedDisc();
        if (shaolin::options.setup || input.empty() || !std::filesystem::exists(input)) {
            if (!shaolin::setup::ChooseDisc(input)) return false;
            remember = true;
        }
    }
    const auto failure = [&](const std::string& message) {
        LOGE("Game setup: %s", message.c_str());
        if (!shaolin::options.validate_disc)
            shaolin::paths::ReportError(std::filesystem::path(std::u8string(message.begin(), message.end())).wstring());
        return false;
    };
    if (input.empty()) return failure("Select your Xbox game with --setup.");
    shaolin::setup::Source source;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!shaolin::setup::PrepareGame(input, source, bytes, error,
                                    !shaolin::options.background && !shaolin::options.validate_disc))
        return failure(error);
    if (remember && shaolin::launcher::ShouldPersist() && !shaolin::setup::RememberDisc(source.path, error)) return failure(error);
    if (source.folder) {
        kernel::MountDisc(nullptr);
        game = {source.path, std::move(bytes)};
    } else {
        auto disc = std::make_unique<kernel::Disc>();
        if (!disc->Open(source.path, error)) return failure(error);
        kernel::MountDisc(std::move(disc));
        game = {shaolin::paths::Cache() / "dvd-scratch", std::move(bytes)};
    }
    LOGR("Verified Shaolin Monks game files");
    return true;
}
