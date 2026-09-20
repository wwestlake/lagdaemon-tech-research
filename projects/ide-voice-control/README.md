# Voice Control for a BYOK IDE Assistant

This research examines what is required to add speech-to-text (STT), text-to-speech (TTS), and eventually conversational voice control to a Windows JUCE IDE whose assistant already supports bring-your-own-key LLM providers.

Start with `VOICE_CONTROL_RESEARCH.md`. Its principal recommendation is to keep text as the stable boundary around the existing LLM provider:

```text
microphone -> STT -> existing text assistant -> TTS -> speakers
```

This lets local and cloud speech engines coexist without coupling voice support to any one LLM vendor.
