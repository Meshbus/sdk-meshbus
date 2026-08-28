# ZUI Predictive Dictionaries

`en_subtlex_words.txt` is the default English source list for ZUI 12-key
predictive input. The build-time generator converts this text file into compact
read-only C data and applies `CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SIZE`.

Source: `words/subtlex-word-frequencies` `index.json`
<https://github.com/words/subtlex-word-frequencies>

License: ISC, copyright (c) 2015 Zeke Sikelianos.

Filtering for the bundled list:

- lowercase `[a-z]` words only
- single-letter words limited to `a` and `i`

The bundled file keeps the full compatible source list. Build-time generation
applies the configured `CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SIZE`,
`CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_CANDIDATES`, and
`CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN` limits.
