# STLT-VJ

A live video player for VJs, built around HAP: a clip deck with layers, columns and decks, beat-synced effects and animations, FFGL plugins, keyboard, MIDI and OSC control, and an Advanced Output with slices, warps, masks and soft edges.

![STLT-VJ: the clip deck on top, monitors, composition, clip and performance panels below](docs/images/stlt-vj.png)

> 🇮🇹 STLT-VJ è un programma per VJ: griglia di clip con layer, colonne e deck, effetti e animazioni a tempo di BPM, plugin FFGL, comandi da tastiera, MIDI e OSC, e un Advanced Output con slice, warp, maschere e soft edge. La guida completa è in [app/README.md](app/README.md).

Everything about the program (features, shortcuts, building) is in **[app/README.md](app/README.md)**. The source is in [app/](app/).

STLT-VJ is an independent project. It is not affiliated with or endorsed by Resolume or by the makers of any other product.

## Building

CMake 3.20+ and a C++17 compiler; see [app/README.md](app/README.md#building) for the platform packages.

```bash
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build --config Release --parallel
```

Every push builds Windows, macOS and Linux versions and runs the tests (GitHub Actions, *STLT-VJ app*); the builds are attached to each run.

## License

GPL-3.0: see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
