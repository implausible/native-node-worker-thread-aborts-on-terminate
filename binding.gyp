{
    "targets": [{
        "target_name": "native-node-tests",

        "sources": [
            "addon.cc"
        ],

        "include_dirs": [
            "<!(node -e \"require('nan')\")"
        ]
    }]
}
