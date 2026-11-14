#pragma once
#include <Arduino.h>
enum class Key : uint8_t { None, Up, Down, Menu, Select, Previous, Next, PlayPause, HoldChanged };
enum class Page : uint8_t { Home, Music, Playlists, Artists, Albums, Genres, Songs,
   Folders, Category, NowPlaying, Settings, Clicker, FontSmoothing, Extras, Clock, SleepTimer, About, WiFi, WiFiNetworks, WiFiPassword, Bluetooth, Storage, StorageEject, StorageFormat1, StorageFormat2 };
enum class Category : uint8_t { Artist, Album, Genre, Folder, Playlist };
enum class RepeatMode : uint8_t { Off=0, One=1, All=2 };
enum class ClickerMode : uint8_t { Off=0, Speaker=1, Headphones=2, Both=3 };
struct Track {
  String path, title, artist, album, genre, folder;
};
struct SettingsState {
  uint8_t volume = 30;
  uint8_t backlightSeconds = 10; // screen off after inactivity; 0=always on
  bool shuffle = false;
  RepeatMode repeat = RepeatMode::Off;
  ClickerMode clicker = ClickerMode::Speaker; // route, not the fitted output hardware
  bool fontSmoothing = false; // default: crisp 1-bit rasterization
  uint8_t contrast = 2;    // monochrome palette; not TFT physical contrast
};
