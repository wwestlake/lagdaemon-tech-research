# Voice Control for a BYOK LLM in a Windows IDE

Status: technology research

Date: 2026-09-20

## Executive answer

Adding useful voice interaction does not require replacing the IDE's existing BYOK LLM integration. The clean first architecture is a chained text pipeline:

```text
JUCE microphone capture
        |
        v
speech endpointing / push-to-talk
        |
        v
SpeechToTextProvider
        |
        v
editable transcript
        |
        v
existing BYOK LLM provider + help/policy/process context
        |
        v
TextToSpeechProvider
        |
        v
JUCE audio playback
```

Text remains the canonical request and response. Voice is an input and output adapter. This preserves the current provider system, transcript, LiteSemRAG context, policies, processes, auditability, and later tool calling.

The recommended first experiment is push-to-talk, not an always-listening full-duplex assistant. It needs:

- JUCE microphone capture and speaker playback;
- a small speech-session controller;
- provider interfaces for STT and TTS;
- one local speech implementation or one cloud implementation;
- visible/editable transcription before submission;
- streamed or sentence-chunked speech playback;
- cancellation and clear session states;
- timing and accuracy instrumentation.

This is a moderate integration, not a new AI system. Continuous natural conversation is a second project because it adds voice activity detection, interruption, echo cancellation, overlapping audio, partial transcripts, and turn arbitration.

## Meaning of "synthesize a voice"

There are two different goals:

1. **Speak with a selected synthetic voice.** Choose a built-in or model-provided voice and generate audio from text. This is straightforward and belongs in the first implementation.
2. **Create a unique cloned or branded voice.** Train or enroll a voice from a person's recordings. This is a separate undertaking involving consent, biometric data, provider restrictions, storage, and disclosure.

The IDE does not need voice cloning to have a strong spoken personality. A high-quality prebuilt local or cloud voice plus consistent speaking instructions is enough for the first system.

## What the IDE must add

### Fit with the current FRust IDE

The current assistant already has the correct textual insertion point. `AiChatPanel` sends a text history through the selected `AiProvider`, and the LiteSemRAG material is attached to the current text request. Voice input should populate that same user-text path; voice output should consume the returned assistant text.

The IDE presently links JUCE's core and GUI modules but not its audio modules. Voice work therefore adds audio-device initialization and the audio module targets rather than replacing the assistant or provider library.

The current provider call returns a complete response instead of streaming tokens. That is sufficient for the first prototype: synthesize after the complete answer arrives. Speaking while the answer is still being generated requires a later streaming callback in `AiProvider`, sentence-boundary buffering, and cancellation propagation. It is not necessary to validate STT, TTS, or the voice UX.

### Audio capture and playback

JUCE already supplies the appropriate Windows audio boundary. `AudioDeviceManager` manages the selected device and continuously delivers audio through `AudioIODeviceCallback`. The callback runs on a high-priority audio thread.

The callback must do very little:

- copy microphone samples into a preallocated ring buffer;
- read ready TTS samples from a playback buffer;
- update lock-free level/status values;
- never call a network service, model, logger, JSON parser, or UI function;
- avoid blocking locks, heap allocation, and file access.

A worker thread consumes microphone samples, converts them to the speech provider's format, and invokes STT. Another worker receives or generates TTS audio and queues it for playback.

The initial JUCE dependencies are likely:

```text
juce_audio_basics
juce_audio_devices
juce_audio_formats
```

`juce_dsp` is optional for later resampling, filtering, denoising, or level processing.

### Audio normalization

Speech providers do not all accept the device's native format. The internal capture contract should be explicit:

```text
mono float PCM blocks at the device sample rate
    -> resample
mono signed 16-bit PCM at 16 kHz for common STT providers
```

Azure's streaming input documentation, for example, specifies mono signed 16-bit PCM at 8 or 16 kHz. Local engines may accept floating-point samples directly. Conversion belongs in the provider adapter or a shared speech-audio utility, not in the real-time callback.

TTS providers may return PCM, WAV, MP3, Opus, or another encoded stream. Normalize output to a queued floating-point PCM format before the audio callback consumes it.

## Provider-neutral interfaces

The LLM key and speech keys are separate concerns. A user might use a local LLM with cloud speech, an Anthropic LLM with OpenAI TTS, an OpenAI LLM with local speech, or an entirely offline stack.

The assistant configuration should therefore select three independent profiles:

```text
LLM provider
STT provider
TTS provider / voice
```

Conceptual C++ interfaces:

```cpp
struct SpeechAudioChunk {
    std::span<const std::int16_t> samples;
    int sampleRate;
    bool endOfUtterance;
};

struct TranscriptUpdate {
    std::string text;
    bool isFinal;
    float confidence;
};

class SpeechToTextProvider {
public:
    virtual void begin(const SpeechRecognitionOptions&) = 0;
    virtual void pushAudio(const SpeechAudioChunk&) = 0;
    virtual void finish() = 0;
    virtual void cancel() = 0;
};

class TextToSpeechProvider {
public:
    virtual void synthesize(const SpeechRequest&, AudioChunkCallback) = 0;
    virtual void cancel() = 0;
};
```

The real interface should report asynchronous partial results, final results, errors, provider timings, audio format, and cancellation. It should never expose provider-specific JSON to the UI.

## Session state

Use an explicit state machine:

```text
Idle
  -> Listening
  -> Transcribing
  -> ReadyToSend
  -> WaitingForLLM
  -> Speaking
  -> Idle
```

Every active state can move to `Cancelled` or `Error`. A new push-to-talk action while speaking should stop playback first. The state must be visible to the UI so a microphone button cannot appear idle while audio is still being captured.

The existing text transcript remains authoritative. STT inserts text into the same input editor used for typing. The user can correct names, paths, punctuation, and code before submission. LLM responses remain text messages even when TTS also speaks them.

## Interaction modes

### Mode 1: push-to-talk

Recommended first implementation:

1. Press or hold the microphone control.
2. Capture audio.
3. Release or press Stop.
4. Finalize transcription.
5. Display editable text.
6. Send explicitly, or enable an optional send-on-release preference.
7. Feed the ordinary LLM response to TTS.
8. Stop speech immediately when requested.

This avoids wake words, false activation, complex endpointing, and feedback from the speakers into the microphone.

### Mode 2: hands-free turn taking

Add voice activity detection (VAD) to begin and end utterances automatically. This requires configurable silence thresholds and protection against clipping a slow speaker or sending on a short pause.

### Mode 3: full duplex with interruption

Natural overlapping conversation requires:

- streaming partial STT;
- streamed LLM output;
- low-latency incremental TTS;
- barge-in detection;
- immediate cancellation of LLM and TTS work;
- acoustic echo cancellation so synthesized speech is not transcribed as the user;
- turn ownership and recovery after interruption.

This mode should wait until push-to-talk proves recognition quality, voice quality, and real usage patterns.

## Engine choices

### Local unified stack: sherpa-onnx

`sherpa-onnx` is the most complete local research candidate. Its native C/C++ APIs cover streaming and offline recognition, VAD, TTS, punctuation, resampling, speech enhancement, and speaker features. Windows prebuilt libraries are available, including builds with TTS.

Advantages:

- audio stays on the machine;
- one native runtime can cover STT, VAD, and TTS;
- supports streaming recognition rather than only file-sized jobs;
- C and C++ APIs fit a JUCE application;
- multiple model families can be tested behind the same adapter.

Costs:

- native binaries and model packages increase distribution size;
- model choice, CPU use, startup time, and quality must be benchmarked on target machines;
- the Apache-2.0 engine license does not automatically settle every model's license. Each selected model package must be reviewed separately.

For local TTS, sherpa-onnx exposes Kokoro, VITS/Piper, Matcha, Kitten, ZipVoice, Pocket, and Supertonic families. Kokoro is a plausible quality baseline with multiple voices, but the documented English package is hundreds of megabytes.

### Local STT specialist: whisper.cpp

`whisper.cpp` is an MIT-licensed C/C++ implementation of Whisper. Its microphone example samples audio continuously and supports a basic VAD-driven sliding-window mode.

Advantages:

- mature, direct native integration;
- strong general dictation and technical-language baseline;
- model sizes and quantizations offer quality/speed choices;
- no audio leaves the machine.

Costs:

- it solves STT, not TTS;
- the example's real-time behavior is repeated windowed inference, not inherently a complete conversational session layer;
- endpointing, partial-result stability, and CPU latency still belong to the application.

A practical combination is whisper.cpp for STT and Windows or sherpa-onnx/Kokoro for TTS.

### Windows built-in speech

Windows exposes `SpeechRecognizer` and `SpeechSynthesizer` through WinRT. `SpeechSynthesizer` can produce an audio stream from text or SSML and select among installed Microsoft-signed voices. It is an excellent zero-key TTS baseline.

Windows free-text dictation is less attractive as the primary STT engine: Microsoft's documentation says predefined dictation uses a remote service, requires Online Speech Recognition, is optimized for short phrases, and recognizes up to ten seconds per input. It is still useful for a fast platform experiment or constrained voice commands.

Advantages:

- already part of Windows;
- no separate TTS model distribution;
- SSML provides rate, pitch, pauses, emphasis, and pronunciation control;
- C++/WinRT can be called from the Windows-only C++ IDE.

Costs:

- available voices vary with installed language packs;
- only Microsoft-signed installed voices are available through this API;
- STT privacy/settings/network behavior is not equivalent to an offline local engine.

### Cloud speech providers

Cloud adapters provide strong quality with no model packaging. They require API keys, network connectivity, usage accounting, privacy disclosure, cancellation, retry behavior, and secure credential storage.

OpenAI's Audio API exposes transcription and speech endpoints, while the Realtime API offers low-latency audio over WebRTC, WebSocket, or SIP with server-side VAD. Azure Speech accepts microphone, file, or pushed audio streams and provides STT/TTS through its SDK.

For a BYOK IDE, implement these as optional speech-provider profiles. Do not route voice through whichever company happens to provide the LLM unless the user deliberately selects that speech provider.

### Direct speech-to-speech models

A realtime multimodal model can consume audio and emit audio directly. This can provide more natural timing and vocal expression, but it weakens the provider-neutral boundary:

- the voice provider may also become the reasoning provider;
- exact text transcripts and deterministic context insertion become less straightforward;
- switching among arbitrary BYOK text LLMs is harder;
- policy, process, RAG evidence, and tool-call auditing must still be preserved.

Direct speech-to-speech is worth supporting later as another session backend. It should not be the foundation of the first BYOK implementation.

## Recommended first implementation

### Local-first research configuration

- JUCE `AudioDeviceManager` for microphone and speakers.
- Push-to-talk with an editable transcript.
- sherpa-onnx as the first unified STT/TTS experiment, because one native adapter can exercise the whole architecture.
- Compare whisper.cpp as the STT baseline if sherpa recognition quality is insufficient.
- Compare Windows `SpeechSynthesizer` as a nearly zero-install TTS baseline.
- Keep the existing BYOK LLM provider untouched.

This recommendation is for research velocity, not a final vendor commitment.

### Cloud comparison configuration

Add one cloud adapter only after the local path works. Use it to measure the quality and latency ceiling, not to redesign the session controller. OpenAI Audio or Azure Speech are both suitable comparison targets because they expose both STT and TTS capabilities.

## Speaking LLM output well

Reading raw Markdown aloud produces a poor experience. The visual answer and spoken rendering should derive from the same response but need not be byte-identical.

Before TTS:

- remove Markdown delimiters;
- skip URLs while saying that a link is available on screen;
- summarize long code blocks rather than reading punctuation character by character;
- pronounce identifiers and acronyms through a configurable lexicon;
- split output at sentence boundaries for streamed synthesis;
- preserve warnings and confirmations;
- stop after a reasonable spoken length while leaving the full response visible.

Do not ask the LLM for a second unsupported paraphrase merely to make speech prettier. Prefer deterministic text normalization, with optional structured response fields later:

```json
{
  "displayText": "...full Markdown answer...",
  "spokenText": "...concise equivalent..."
}
```

If `spokenText` is model-generated, it remains part of the response record and must not omit safety-critical information.

## Voice control and future tools

Speech should produce the same user-intent text as typing. It must not bypass policy, process, confirmation, or tool authorization.

```text
spoken request
    -> transcript
    -> agent interpretation
    -> target resolution
    -> confirmation when required
    -> tool call
    -> observed result
    -> spoken and visual response
```

This is particularly important when the agent must infer targets such as "the vocal track" or "the track I just added." Recognition confidence is not target-resolution confidence. The system can transcribe every word correctly and still choose the wrong object.

Destructive or ambiguous actions should display the interpreted command and target before execution. A spoken confirmation can be accepted later, but there must always be a visible record.

## Domain vocabulary

Programming and media applications contain names ordinary STT models often miss: `FRust`, `Frate`, pod names, symbols, filenames, track names, plug-ins, and error codes.

Maintain a dynamic speech lexicon from:

- open project symbols and filenames;
- help topic titles and keywords;
- visible track, device, and plug-in names;
- registered tool and command names;
- recent conversation entities;
- user-defined pronunciation entries.

Providers that support phrase hints or contextual biasing should receive this vocabulary. Providers that do not should use a conservative transcript-correction pass that shows every changed token. Never silently rewrite paths, identifiers, numbers, or quoted text.

## Privacy and security

- Display whether recognition and synthesis are local or cloud-backed.
- Request microphone access only when voice is enabled.
- Do not retain microphone audio by default.
- Make diagnostic audio recording explicit and time-limited.
- Store speech keys using the same protected credential mechanism as LLM keys.
- Redact credentials and sensitive paths from provider logs.
- Cancel and discard pending audio when a profile changes or the session closes.
- Never send background microphone audio while idle.
- Record provider, model, locale, voice, and timing metadata with the transcript, but not raw audio unless opted in.

## Custom or cloned voice

A unique voice can be added later through a provider-specific `voiceId`, leaving the TTS interface unchanged. It should have its own governance:

- documented identity of the voice talent;
- explicit consent and permitted uses;
- training-recording provenance;
- access control and revocation;
- disclosure that the voice is synthetic;
- retention and deletion rules;
- protection against exporting or misusing the model.

Azure's custom neural voice is a limited-access feature and requires voice-talent consent. Its documentation distinguishes a 20-50-utterance evaluation path from professional fine-tuning using hundreds or thousands of utterances. That scale and governance make custom voice a product decision, not a prerequisite for IDE voice support.

## Measurement plan

Build a small repeatable utterance set before choosing engines:

- ordinary questions;
- FRust syntax and library terms;
- file paths and identifiers;
- numbers, versions, and error codes;
- short commands;
- long explanatory requests;
- corrections and interruptions;
- quiet room, keyboard noise, and speaker playback conditions.

Capture these timings:

```text
speech end -> final transcript
send -> first LLM token
LLM text available -> first synthesized audio
speech end -> first audible response
cancel request -> silence
```

Evaluate:

- exact domain-token accuracy;
- intent preservation;
- correction frequency;
- target-selection accuracy once tools exist;
- end-to-end task success;
- false activation rate for hands-free mode;
- subjective voice clarity and fatigue;
- CPU, memory, model size, and first-use download cost.

Word error rate alone is insufficient. Misrecognizing one track name, path, numeric value, or negation can matter more than several harmless filler-word errors.

## Concrete development sequence

### Experiment A: audio plumbing

- Add a global JUCE audio device manager.
- Capture microphone audio into a ring buffer.
- Play a generated or bundled PCM response.
- Add device selection and input/output level indicators.
- Verify no allocation or blocking in the audio callback.

### Experiment B: push-to-talk transcription

- Add `SpeechToTextProvider`.
- Integrate one local recognizer.
- Display partial/final text and let the user edit it.
- Instrument endpoint-to-transcript latency.

### Experiment C: spoken responses

- Add `TextToSpeechProvider` and voice selection.
- Normalize assistant Markdown for speech.
- Synthesize sentence chunks on a worker.
- Add Stop and disable/enable speech controls.

### Experiment D: provider comparison

- Add one cloud STT/TTS adapter.
- Run the same utterance and response corpus.
- Decide defaults based on measured quality, latency, privacy, packaging, and cost.

### Experiment E: conversational behavior

- Add VAD-based automatic endpointing.
- Add streamed partial transcription.
- Add interruption and cancellation.
- Add echo control only when speaker/microphone feedback becomes a measured issue.

## Relative effort and risk

| Capability | Engineering level | Main risk |
|---|---|---|
| Speak a supplied text string with a Windows voice | Small | Voice availability differs by installed language |
| Push-to-talk capture and local/cloud transcription | Moderate | Audio threading, resampling, domain vocabulary |
| Speak complete LLM responses | Moderate | Cancellation and making Markdown sound natural |
| Stream transcription and speech | Moderate-high | Partial-result correction and buffering |
| Hands-free automatic turns | High | Endpointing and false activation |
| Full duplex with barge-in | High | Echo cancellation and coordinated cancellation |
| Train or enroll a unique voice | Separate product effort | Consent, model rights, storage, and provider access |

The smallest convincing demonstration is therefore: press the microphone button, speak a question, edit or send the transcript, receive the normal BYOK answer, and press a speaker control to hear it. Everything after that can be justified by observed use rather than assumed requirements.

## Decisions to make after the prototype

- Whether local speech is bundled or downloaded on demand.
- Which model and voice licenses permit redistribution.
- Whether STT and TTS can use different providers simultaneously.
- Whether send-on-release is opt-in or default.
- Whether the spoken answer is full, concise, or user-selectable.
- How speech profiles share configuration with existing BYOK profiles.
- Whether hands-free mode is valuable enough to justify VAD and echo cancellation.
- Whether a custom voice adds enough value to justify its consent and lifecycle obligations.

## Bottom line

The first useful version is not difficult conceptually:

```text
push-to-talk -> STT -> existing assistant -> TTS
```

The work is mainly reliable audio threading, asynchronous cancellation, provider abstraction, transcript UX, and testing domain vocabulary. The hard part begins only when the IDE must behave like an open-microphone conversational partner that can interrupt, be interrupted, and operate tools safely.

Keeping text as the canonical boundary gives the project a useful voice quickly while preserving BYOK freedom and the context system already being developed.

## Primary sources

- [JUCE AudioDeviceManager](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html)
- [JUCE AudioIODeviceCallback](https://docs.juce.com/master/classjuce_1_1AudioIODeviceCallback.html)
- [sherpa-onnx native C/C++ API](https://k2-fsa.github.io/sherpa/onnx/c-api/html/index.html)
- [sherpa-onnx Windows binaries](https://k2-fsa.github.io/sherpa/onnx/install/windows/generated/download/windows_x86.html)
- [sherpa-onnx TTS model APIs](https://k2-fsa.github.io/sherpa/onnx/c-api/html/tts.html)
- [sherpa-onnx model-license warning](https://k2-fsa.github.io/sherpa/onnx/tts/apk.html)
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- [whisper.cpp microphone streaming example](https://github.com/ggml-org/whisper.cpp/blob/master/examples/stream/README.md)
- [Windows SpeechRecognizer](https://learn.microsoft.com/en-us/windows/apps/develop/input/speech-recognition)
- [Windows SpeechSynthesizer](https://learn.microsoft.com/en-us/uwp/api/windows.media.speechsynthesis.speechsynthesizer)
- [Azure Speech audio input streams](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/how-to-use-audio-input-streams)
- [OpenAI Audio API](https://developers.openai.com/api/reference/cli/resources/audio)
- [OpenAI Realtime API](https://platform.openai.com/docs/api-reference/realtime)
- [Azure custom voice overview](https://learn.microsoft.com/en-us/azure/ai-services/Speech-Service/custom-neural-voice)
- [Azure custom voice limited access and consent](https://learn.microsoft.com/en-us/azure/foundry/responsible-ai/speech-service/text-to-speech/limited-access)
