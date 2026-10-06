# fnv-libsm64

Play as Mario in Fallout: New Vegas. This xNVSE plugin runs Super Mario 64's own movement code through
[libsm64](https://github.com/libsm64/libsm64), collides it with the game's Havok geometry, draws Mario with
textures read from your ROM, and plays his voice and sound effects.

**Status: early prototype.** It is developed and tested in Doc Mitchell's house in Goodsprings, on the GOG
release running under Proton on Linux.

## Requirements

- Fallout: New Vegas 1.4.0.525 (Steam or GOG) with [xNVSE](https://github.com/xNVSE/NVSE) (tested with 6.4.9).
- Your own Super Mario 64 US ROM in .z64 format. It is not included. The plugin refuses any file whose
  SHA-256 is not `17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`.
- To build: CMake 3.24 or newer, Ninja, the i686 mingw-w64 toolchain and Python 3.

## Build

```sh
git clone --recursive https://github.com/larsoyd/fnv-libsm64
cd fnv-libsm64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
cmake --build build --target sm64nv
```

The plugin is `build/sm64nv.dll`.

## Install

1. Copy `sm64nv.dll` to `Data\NVSE\Plugins\` in the game folder.
2. Create `Data\NVSE\Plugins\sm64nv.ini`:

   ```ini
   rom=C:\path\to\baserom.us.z64
   autotake=1
   ```

   Under Wine or Proton, give the ROM path as a Windows path, for example `Z:\home\you\roms\baserom.us.z64`.
   Optional keys:
   - `autotake=1` hands you Mario each time a save finishes loading or you arrive in a new cell. With
     `autotake=0` (the default) you switch with M or D-pad down.
   - `cell=GSDocMitchellHouse` jumps to that cell shortly after the main menu appears.
   - `scale` sets SM64 units per game unit (default 1.5).
3. Start the game and load a save.

The plugin writes its log to `sm64nv.log` in the game folder. Any problem it finds is a line starting
with `refused:`.

## Controls

| Action | Keyboard and mouse | Xbox controller |
|---|---|---|
| Switch between Mario and the Courier | M | D-pad down |
| Move | WASD | Left stick (analog) |
| Jump (A) | Space | A |
| Punch, dive (B) | Shift or left mouse button | X |
| Crouch (Z) | Ctrl | Either trigger |
| Camera | Mouse (untested without a controller) | Right stick |

Mario ignores input while a menu, the Pip-Boy or the console is open.

## Known limitations

- Only the cell where Mario takes over has collision. Changing cells hands control back to the Courier,
  and Mario takes over again in the new cell (with `autotake=1`, otherwise press M).
- No exterior terrain, water, doors or combat while Mario is active.
- Saving while Mario is active stores the Courier's own controls and camera, so the save also loads
  without the plugin.

## Credits

- [libsm64](https://github.com/libsm64/libsm64) (CC0), built on the Super Mario 64 decompilation project.
- The [xNVSE](https://github.com/xNVSE/NVSE) team.
- Super Mario 64 belongs to Nintendo. This project ships no Nintendo data. Mario's model, textures and
  sounds come from your own ROM at runtime.

## License

MIT, see [LICENSE](LICENSE). libsm64 is included as a submodule under its own CC0 license.
