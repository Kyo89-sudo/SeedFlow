// Exercise installation against an isolated registry subtree and temp folders.
#define SF_INSTALLER_TEST
#include "../installer/main.cpp"
#include <fstream>
#include <iostream>

static void check(bool value, char const* message) {
    if (!value) throw std::runtime_error(message);
    std::cout << "OK " << message << '\n';
}
int wmain() {
    HKEY hive = nullptr;
    std::wstring const hive_path = L"Software\\SeedFlowInstallerTest-" + std::to_wstring(GetCurrentProcessId());
    fs::path const fixture = fs::temp_directory_path() /
        (L"SeedFlowInstallerTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    bool redirected = false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int exit_code = 1;
    try {
        check(RegCreateKeyExW(HKEY_CURRENT_USER, hive_path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                             nullptr, &hive, nullptr) == ERROR_SUCCESS, "create isolated registry");
        check(RegOverridePredefKey(HKEY_CURRENT_USER, hive) == ERROR_SUCCESS, "redirect HKCU");
        redirected = true;
        instance = GetModuleHandleW(nullptr);
        test_root = fixture;
        destination = fixture / L"program";
        registry(L"Software\\Classes\\.torrent", nullptr, L"OtherClient.Torrent");
        registry(L"Software\\Classes\\magnet\\shell\\open\\command", nullptr, L"OtherClient.exe \"%1\"");
        fs::create_directories(fixture / L"State");
        std::ofstream(fixture / L"State" / L"settings.dat") << "preserve";
        install(true, true, true);
        check(!progress_events.empty() && progress_events.front() == 0 && progress_events.back() == 100 &&
              std::is_sorted(progress_events.begin(), progress_events.end()), "installation progress reaches 100 monotonically");
        check(fs::file_size(destination / L"SeedFlow.exe") == SizeofResource(instance,
              FindResourceW(instance, MAKEINTRESOURCEW(201), RT_RCDATA)), "embedded application installed");
        check(fs::exists(destination / L"Uninstall.exe") && fs::exists(destination / L"LICENSE"),
              "uninstaller and license installed");
        check(registry_text(kInstallKey, L"DisplayVersion") == SF_VERSION_WSTRING, "version registered");
        check(registry_text(L"Software\\Classes\\SeedFlow.Magnet\\shell\\open\\command", nullptr) ==
              quote((destination / L"SeedFlow.exe").wstring()) + L" \"%1\"", "quoted magnet command");
        check(registry_text(L"Software\\Classes\\.torrent", nullptr) == L"OtherClient.Torrent" &&
              registry_text(L"Software\\Classes\\magnet\\shell\\open\\command", nullptr) == L"OtherClient.exe \"%1\"",
              "existing defaults preserved");
        HANDLE locked = CreateFileW((destination / L"SeedFlow.exe").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "lock destination to simulate update failure");
        progress_events.clear();
        bool failed = false;
        try { install(true, true, true); } catch (std::exception const&) { failed = true; }
        CloseHandle(locked);
        check(failed && !progress_events.empty() && progress_events.back() < 100,
              "failed replacement does not report completion");
        progress_events.clear();
        install(true, true, true);
        check(fs::exists(test_root / L"desktop" / L"SeedFlow.lnk"), "repeat install and desktop shortcut");
        progress_events.clear();
        uninstall();
        check(progress_events.front() == 0 && progress_events.back() == 100 &&
              std::is_sorted(progress_events.begin(), progress_events.end()), "uninstall progress reaches 100");
        check(!fs::exists(destination / L"SeedFlow.exe") && !fs::exists(destination / L"Uninstall.exe"),
              "installed executables removed");
        check(registry_text(kInstallKey, L"DisplayVersion").empty() &&
              registry_text(L"Software\\Classes\\SeedFlow.Magnet\\shell\\open\\command", nullptr).empty(),
              "registration removed");
        check(fs::exists(fixture / L"State" / L"settings.dat"), "personal state preserved");
        install(false, false, false);
        check(registry_text(L"Software\\Classes\\SeedFlow.Torrent\\shell\\open\\command", nullptr).empty() &&
              registry_text(L"Software\\Classes\\SeedFlow.Magnet\\shell\\open\\command", nullptr).empty(),
              "optional associations disabled");
        uninstall();
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&common);
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = instance;
        cls.lpszClassName = L"SeedFlowProgressTest";
        check(RegisterClassW(&cls) != 0, "register hidden progress window");
        window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                 0, 0, 660, 520, nullptr, nullptr, instance, nullptr);
        check(window != nullptr && !(GetWindowLongPtrW(progress, GWL_STYLE) & PBS_MARQUEE),
              "installer uses determinate progress");
        SendMessageW(defaults_box, BM_SETCHECK, BST_UNCHECKED, 0);
        busy = true;
        SendMessageW(window, kProgress, 50, reinterpret_cast<LPARAM>(L"Test"));
        check(SendMessageW(progress, PBM_GETPOS, 0, 0) == 50, "UI displays actual progress");
        worker = std::jthread([] {});
        result_message = L"Test completato.";
        SendMessageW(window, kComplete, TRUE, 0);
        check(SendMessageW(progress, PBM_GETPOS, 0, 0) == 100, "UI stays at 100 on completion");
        DestroyWindow(window);
        window = nullptr;
        DeleteObject(normal_font);
        DeleteObject(title_font);
        exit_code = 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; }
    if (redirected) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    if (hive) RegCloseKey(hive);
    RegDeleteTreeW(HKEY_CURRENT_USER, hive_path.c_str());
    std::error_code ignored;
    fs::remove_all(fixture, ignored);
    CoUninitialize();
    return exit_code;
}
