"""Toggle voice-mode orchestration independent of audio hardware and application tools."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any, Callable, Protocol, Sequence


class VoiceModeState(str, Enum):
    OFF = "off"
    STARTING = "starting"
    LISTENING = "listening"
    TRANSCRIBING = "transcribing"
    THINKING = "thinking"
    SPEAKING = "speaking"
    STOPPING = "stopping"
    ERROR = "error"


@dataclass(frozen=True)
class AudioFrame:
    samples: Sequence[float]
    sample_rate: int
    channels: int = 1


@dataclass(frozen=True)
class Transcript:
    text: str
    confidence: float | None = None


@dataclass(frozen=True)
class EngineerResponse:
    display_text: str
    spoken_text: str | None = None
    diagnostics: dict[str, Any] | None = None


class AudioRouteLease(Protocol):
    """Exclusive temporary route for the Engineer mic/headset conversation."""

    def restore(self) -> None: ...


class AudioRouteManager(Protocol):
    def acquire_engineer_route(self) -> AudioRouteLease: ...


class SpeechInput(Protocol):
    """STT owns endpointing while voice mode is active."""

    def start(self, on_utterance: Callable[[Transcript], None]) -> None: ...
    def stop(self) -> None: ...
    def cancel(self) -> None: ...


class EngineerAgent(Protocol):
    def respond(self, transcript: str) -> EngineerResponse: ...
    def cancel(self) -> None: ...


class SpeechOutput(Protocol):
    def speak(self, text: str, on_complete: Callable[[], None]) -> None: ...
    def cancel(self) -> None: ...


class EngineerVoiceController:
    """A press-on/press-off voice session.

    The first toggle leases the Engineer route and begins listening. STT endpointing
    can produce any number of utterances. The second toggle cancels in-flight work,
    restores the previous route exactly once, and returns to OFF.

    The callbacks here are synchronous reference contracts. Production adapters may
    invoke them from worker threads and must marshal `on_state_changed` to the UI.
    """

    def __init__(
        self,
        routes: AudioRouteManager,
        speech_input: SpeechInput,
        agent: EngineerAgent,
        speech_output: SpeechOutput,
        *,
        on_state_changed: Callable[[VoiceModeState], None] | None = None,
        on_transcript: Callable[[Transcript], None] | None = None,
        on_response: Callable[[EngineerResponse], None] | None = None,
        on_error: Callable[[Exception], None] | None = None,
    ) -> None:
        self.routes = routes
        self.speech_input = speech_input
        self.agent = agent
        self.speech_output = speech_output
        self.on_state_changed = on_state_changed
        self.on_transcript = on_transcript
        self.on_response = on_response
        self.on_error = on_error
        self.state = VoiceModeState.OFF
        self._lease: AudioRouteLease | None = None
        self._generation = 0

    @property
    def active(self) -> bool:
        return self.state is not VoiceModeState.OFF

    def toggle(self) -> None:
        """Concrete IDE binding: call this on each F9 key-down event."""
        if self.active:
            self.deactivate()
        else:
            self.activate()

    def activate(self) -> None:
        if self.active:
            return
        self._generation += 1
        generation = self._generation
        self._set_state(VoiceModeState.STARTING)
        try:
            self._lease = self.routes.acquire_engineer_route()
            self.speech_input.start(
                lambda transcript: self._handle_utterance(transcript, generation)
            )
            self._set_state(VoiceModeState.LISTENING)
        except Exception as error:
            self._fail(error)

    def deactivate(self) -> None:
        if not self.active:
            return
        self._generation += 1
        self._set_state(VoiceModeState.STOPPING)
        try:
            self.speech_input.cancel()
            self.agent.cancel()
            self.speech_output.cancel()
        finally:
            self._restore_route()
            self._set_state(VoiceModeState.OFF)

    def _handle_utterance(self, transcript: Transcript, generation: int) -> None:
        if generation != self._generation or not self.active:
            return
        if not transcript.text.strip():
            self._set_state(VoiceModeState.LISTENING)
            return
        try:
            self.speech_input.stop()
            self._set_state(VoiceModeState.TRANSCRIBING)
            if self.on_transcript:
                self.on_transcript(transcript)
            self._set_state(VoiceModeState.THINKING)
            response = self.agent.respond(transcript.text.strip())
            if generation != self._generation or not self.active:
                return
            if self.on_response:
                self.on_response(response)
            spoken = (response.spoken_text or response.display_text).strip()
            if not spoken:
                self._resume_listening(generation)
                return
            self._set_state(VoiceModeState.SPEAKING)
            self.speech_output.speak(
                spoken, lambda: self._speech_complete(generation)
            )
        except Exception as error:
            self._fail(error)

    def _speech_complete(self, generation: int) -> None:
        if generation == self._generation and self.active:
            self._resume_listening(generation)

    def _resume_listening(self, generation: int) -> None:
        if generation != self._generation or not self.active:
            return
        self.speech_input.start(
            lambda transcript: self._handle_utterance(transcript, generation)
        )
        self._set_state(VoiceModeState.LISTENING)

    def _fail(self, error: Exception) -> None:
        self._set_state(VoiceModeState.ERROR)
        try:
            self.speech_input.cancel()
            self.agent.cancel()
            self.speech_output.cancel()
        finally:
            self._restore_route()
        if self.on_error:
            self.on_error(error)

    def reset_after_error(self) -> None:
        if self.state is VoiceModeState.ERROR:
            self._set_state(VoiceModeState.OFF)

    def _restore_route(self) -> None:
        if self._lease is not None:
            lease, self._lease = self._lease, None
            lease.restore()

    def _set_state(self, state: VoiceModeState) -> None:
        self.state = state
        if self.on_state_changed:
            self.on_state_changed(state)
