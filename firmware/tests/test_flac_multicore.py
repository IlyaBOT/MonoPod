from pathlib import Path

FW=Path(__file__).resolve().parents[1]/'DIY_iPod_Mono'

def test_flac_metadata_and_duration():
    lib=(FW/'Library.cpp').read_text()
    head=(FW/'FlacMetadata.h').read_text()
    assert 'FlacMetadata::parse(f,flac)' in lib
    assert 'flacTotalSamples=flac.totalSamples' in lib
    assert 'VORBIS_COMMENT' in head
    assert 'info.durationSeconds=' in head
    assert 'field[256]' in head  # recursive scanner must not allocate 4KiB stack buffers

def test_real_flac_sample_seek_not_byte_seek():
    cpp=(FW/'AudioEngine.cpp').read_text()
    assert 'flac->seekToSample(sample)' in cpp
    assert 'FLAC__stream_decoder_seek_absolute' in (FW/'SeekableFlacDecoder.h').read_text()
    assert 'source->seek((int32_t)position,SEEK_SET)' in cpp  # MP3/WAV legacy only
    assert 'if(path.endsWith(".flac"))' in cpp

def test_second_core_dedicated_audio_and_sync_protected():
    app=(FW/'DIY_iPod_Mono.ino').read_text()
    assert 'xTaskCreatePinnedToCore(audioCoreWorker' in app
    assert '&monopodAudioTask,0)' in app
    assert 'if(!monopodAudioTask)player.loop();' in app
    engine=(FW/'AudioEngine.cpp').read_text()
    assert 'xSemaphoreTakeRecursive' in engine and 'xSemaphoreGiveRecursive' in engine
    assert 'i2s->SetBuffers(24,512)' in engine
    assert 'player.suspendBackground(true)' in (FW/'SyncMode.cpp').read_text()
    assert 'player.suspendBackground(true)' in (FW/'BadApple.cpp').read_text()

def test_bounded_flac_read_ahead():
    data=(FW/'ChunkedFlacSource.h').read_text()
    assert 'WANT_BYTES=2*1024*1024' in data
    assert 'psramFree/2' in data
    assert 'INITIAL_BYTES=12*1024' in data
    assert 'REFILL_BYTES=4096' in data
    assert 'MALLOC_CAP_SPIRAM' in data
    assert 'if(!src->seek' in data
