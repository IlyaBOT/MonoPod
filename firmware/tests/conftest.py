"""Platform gates only; firmware protocol/FS behavior is checked by actual-source MSVC suites."""
import shutil,pytest
def pytest_collection_modifyitems(items):
    if shutil.which('g++'):return
    gcc_only={'test_console_capacity_cannot_collide_with_limits_h','test_native_protocol_parser','test_public_wifi_header_is_valid_cpp'}
    for item in items:
        if item.name in gcc_only:item.add_marker(pytest.mark.skip(reason='GCC-specific harness unavailable on this Windows host; see MSVC actual-source tests and Arduino builds.'))
