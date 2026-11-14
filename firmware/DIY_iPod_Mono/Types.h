#pragma once
#include <Arduino.h>
#include "Config.h"
enum class Key : uint8_t { None, Up, Down, Menu, Select, SelectLong, Previous, Next, PlayPause, RemotePlay, RemotePause, HoldChanged };
enum class Page : uint8_t { Home, Music, Playlists, Artists, Albums, Genres, Songs,
   Folders, Category, NowPlaying, Settings, Backlight, BacklightTimer, ResetAllSettings, Clicker, FontSmoothing, Extras, Clock, ClockFormat, UsbMode, SleepTimer, About, BadApple, WiFi, WiFiNetworks, WiFiPassword,
#if FEATURE_BLUETOOTH
   Bluetooth, BluetoothPaired, BluetoothScan, BluetoothPairConfirm, BluetoothPin, BluetoothDelete,
#endif
   Storage, StorageEject, StorageFormat1, StorageFormat2, DeleteTrackConfirm };
enum class Category : uint8_t { Artist, Album, Genre, Folder, Playlist };
enum class RepeatMode : uint8_t { Off=0, One=1, All=2 };
enum class UsbMode : uint8_t { MassStorage=0, ITunes=1 };
enum class ClickerMode : uint8_t { Off=0, Speaker=1, Headphones=2, Both=3 };
struct Track {
  String path, title, artist, album, albumArtist, genre, folder, extension, codec;
  uint64_t fileSize=0;
  uint32_t modifiedUnix=0; // FAT last write; 0 means invalid/unknown
  uint32_t fingerprint=0;  // CRC32 of sampled content, NOT full-file integrity
  uint32_t fullCrc32=0;    // 0 = not verified with full-file CRC32
  uint32_t durationMs=0;
  uint32_t sampleRate=0;
  uint32_t bitrateKbps=0;
  uint16_t trackNumber=0;
  uint32_t durationSeconds=0; // 0=unknown; ID3 TLEN or WAV duration
  uint16_t year=0; // 0=unknown; ID3 TDRC/TYER or ID3v1
  uint32_t flacSampleRate=0; // FLAC STREAMINFO, needed for sample-accurate seek
  uint64_t flacTotalSamples=0;
};
struct SettingsState {
  uint8_t volume = 30;
  uint8_t backlightSeconds = 10; // valid values: 5, 10, 15, 30, 60 seconds
  uint8_t backlightBrightness = 75; // percentage, 5..100 (saved in NVS)
  bool shuffle = false;
  RepeatMode repeat = RepeatMode::Off;
  ClickerMode clicker = ClickerMode::Speaker; // route, not the fitted output hardware
  bool fontSmoothing = false; // default: crisp 1-bit rasterization
  UsbMode usbMode = UsbMode::MassStorage;
  uint8_t contrast = 2;    // monochrome palette; not TFT physical contrast
};
