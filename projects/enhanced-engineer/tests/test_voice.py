import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from enhanced_engineer.voice import (
    EngineerResponse,
    EngineerVoiceController,
    Transcript,
    VoiceModeState,
)


class FakeLease:
    def __init__(self):
        self.restore_count = 0

    def restore(self):
        self.restore_count += 1


class FakeRoutes:
    def __init__(self):
        self.acquire_count = 0
        self.lease = FakeLease()

    def acquire_engineer_route(self):
        self.acquire_count += 1
        return self.lease


class FakeSpeechInput:
    def __init__(self):
        self.callback = None
        self.start_count = 0
        self.stop_count = 0
        self.cancel_count = 0

    def start(self, callback):
        self.callback = callback
        self.start_count += 1

    def stop(self):
        self.stop_count += 1

    def cancel(self):
        self.cancel_count += 1
        self.callback = None

    def utter(self, text):
        callback = self.callback
        self.callback = None
        callback(Transcript(text, 0.98))


class FakeAgent:
    def __init__(self):
        self.requests = []
        self.cancel_count = 0
        self.error = None

    def respond(self, transcript):
        if self.error:
            raise self.error
        self.requests.append(transcript)
        return EngineerResponse(
            display_text=f"I heard: {transcript}",
            spoken_text=f"Understood. {transcript}",
        )

    def cancel(self):
        self.cancel_count += 1


class FakeSpeechOutput:
    def __init__(self):
        self.spoken = []
        self.complete = None
        self.cancel_count = 0

    def speak(self, text, on_complete):
        self.spoken.append(text)
        self.complete = on_complete

    def finish(self):
        callback, self.complete = self.complete, None
        callback()

    def cancel(self):
        self.cancel_count += 1
        self.complete = None


class VoiceControllerTests(unittest.TestCase):
    def setUp(self):
        self.routes = FakeRoutes()
        self.input = FakeSpeechInput()
        self.agent = FakeAgent()
        self.output = FakeSpeechOutput()
        self.states = []
        self.controller = EngineerVoiceController(
            self.routes,
            self.input,
            self.agent,
            self.output,
            on_state_changed=self.states.append,
        )

    def test_f9_toggle_holds_route_for_multiple_voice_turns(self):
        self.controller.toggle()
        self.assertEqual(VoiceModeState.LISTENING, self.controller.state)
        self.assertEqual(1, self.routes.acquire_count)

        self.input.utter("lower the guitar track two decibels")
        self.assertEqual(VoiceModeState.SPEAKING, self.controller.state)
        self.assertEqual(1, self.input.stop_count)
        self.assertEqual(["Understood. lower the guitar track two decibels"], self.output.spoken)
        self.output.finish()
        self.assertEqual(VoiceModeState.LISTENING, self.controller.state)
        self.assertEqual(2, self.input.start_count)

        self.input.utter("that is right")
        self.output.finish()
        self.assertEqual(VoiceModeState.LISTENING, self.controller.state)
        self.assertEqual(0, self.routes.lease.restore_count)

        self.controller.toggle()
        self.assertEqual(VoiceModeState.OFF, self.controller.state)
        self.assertEqual(1, self.routes.lease.restore_count)

    def test_second_press_cancels_speech_and_restores_route(self):
        self.controller.toggle()
        self.input.utter("tell me about this track")
        self.assertEqual(VoiceModeState.SPEAKING, self.controller.state)

        self.controller.toggle()

        self.assertEqual(VoiceModeState.OFF, self.controller.state)
        self.assertEqual(1, self.output.cancel_count)
        self.assertEqual(1, self.routes.lease.restore_count)

    def test_failure_restores_route(self):
        errors = []
        self.controller.on_error = errors.append
        self.agent.error = RuntimeError("provider unavailable")
        self.controller.toggle()

        self.input.utter("hello")

        self.assertEqual(VoiceModeState.ERROR, self.controller.state)
        self.assertEqual(1, self.routes.lease.restore_count)
        self.assertEqual("provider unavailable", str(errors[0]))
        self.controller.reset_after_error()
        self.assertEqual(VoiceModeState.OFF, self.controller.state)


if __name__ == "__main__":
    unittest.main()
