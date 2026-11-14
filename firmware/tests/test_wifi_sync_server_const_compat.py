from pathlib import Path

FW = Path(__file__).resolve().parents[1] / "DIY_iPod_Mono"


def test_webserver_client_used_only_from_nonconst_method():
    header = (FW / "WiFiSync.h").read_text(encoding="utf-8")
    implementation = (FW / "WiFiSync.cpp").read_text(encoding="utf-8")
    assert "bool sessionAuthorized();" in header
    assert "bool sessionAuthorized()const;" not in header
    assert "bool WiFiSyncService::sessionAuthorized() {" in implementation
    assert "bool WiFiSyncService::sessionAuthorized() const" not in implementation
    assert "server.client().remoteIP()==requestIP" in implementation
    assert 'server.header("X-MonoPod-Session")==sessionToken' in implementation
