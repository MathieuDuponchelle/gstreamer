# Hacking around the fact that custom_target can't take
# a configuration object as parameter.
cmd = '''import re, sys
with open("@0@", "r") as sources:
    lines = sources.readlines()
with open("@1@", "w") as sources:
    for line in lines:
        sources.write(re.sub("@SCANNER_PATH@", "@2@", line))
'''.format(join_paths(meson.current_source_dir(), 'gst_extension.py'),
           join_paths(meson.current_build_dir(), 'gst_extension.py'),
           hotdoc_plugin_scanner.full_path())

run_command(python3, '-c', cmd)

cfile = custom_target('gst_extension',
                      input: 'gst_extension.py',
                      output : 'gst_extension.py',
                      command: [python3, '-c', cmd],
                      depends: hotdoc_plugin_scanner)
