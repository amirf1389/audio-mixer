{
  "targets": [{
    "target_name": "amfb_mixer",
    "sources": ["addon.cc"],
    "defines": ["NAPI_VERSION=8"],
    "cflags_cc": ["-std=c++20", "-O2"],
    "msvs_settings": {"VCCLCompilerTool": {"AdditionalOptions": ["/std:c++20", "/O2", "/EHsc"]}},
    "conditions": [["OS=='linux'", {"libraries": ["-lrt"]}]]
  }]
}
