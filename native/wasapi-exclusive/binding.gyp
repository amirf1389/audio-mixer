{
  "targets": [
    {
      "target_name": "wasapi_exclusive",
      "sources": [ "src/main.cc", "src/wasapi.cc" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "dependencies": [ "<!(node -p \"require('node-addon-api').gyp\")" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc": [ "-std=c++17", "-Wall", "-Wextra" ],
      "conditions": [
        ["OS=='win'", { "defines": [ "_WIN32_WINNT=0x0600" ], "libraries": [ "-lole32", "-lpropsys" ] }]
      ]
    }
  ]
}
