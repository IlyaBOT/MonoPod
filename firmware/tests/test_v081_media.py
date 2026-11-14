"""Static regressions for the 1024+ track requirement."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_capacity_is_at_least_1024_in_both_profiles():
    for profile in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        config=(ROOT/profile/'Config.h').read_text()
        assert 'MAX_TRACKS = 2048' in config
        assert 'PIN_TFT_RST = -1' in config
        assert 'PIN_TFT_RST = 47' in config

def test_lazy_song_rows_do_not_duplicate_all_titles():
    for profile in ('DIY_iPod_Mono','Wokwi_ILI9341'):
        ui=(ROOT/profile/'UI.cpp').read_text()
        assert 'media.allIndices(songIndices);' in ui
        assert 'media.matching(activeCategory,filter,songIndices);' in ui
        assert 'media.at(songIndices[index]).title' in ui
        assert 'int PlayerUI::listItemCount() const' in ui
        assert 'for(size_t i=0;i<songIndices.size();i++)add(' not in ui
        assert 'index>=listItemCount()' in ui
        assert 'if(selected>=listItemCount())' in ui

def test_board_identification_and_hardware_limit_documented():
    ino=(ROOT/'BoardProbe'/'BoardProbe.ino').read_text()
    assert 'ESP.getChipModel()' in ino
    assert 'ESP.getPsramSize()' in ino
    assert 'NO Bluetooth Classic A2DP/AVRCP' in ino
