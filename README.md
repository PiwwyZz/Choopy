# Choopy (ชูปี้) - Elderly Care AI Assistant 🧓🤖

A Matthayom 4 school project. Choopy is an AI voice assistant designed specifically to converse with the elderly. It listens to Thai voice input, processes the conversation using OpenAI, and speaks back in Thai with a sweet, caring, and concise tone.

## 🚀 Features
- **ASR (Speech-to-Text):** Captures Thai voice using an INMP441 microphone and transcribes it via the OpenAI Whisper API.
- **AI Brain:** Uses OpenAI `gpt-4o-mini` to generate short, caring responses (max 15 words) acting as "Choopy".
- **TTS (Text-to-Speech):** Converts the AI's response back to Thai audio using the Google Translate TTS API.
- **Hardware Controller:** Runs entirely on an ESP32 microcontroller.

## 🔨 Tools & Materials
- ESP32 Board (Model: ESP32-32D) x1
- INMP441 Microphone x1
- MAX98357A Audio Amplifier x1
- Speaker x1
- Push Button x1
- Breadboards (x2) & Jumper Wires

## 🔌 Wiring & Pin Connections

| Component | ESP32 Pin |
| :--- | :--- |
| **INMP441 (Mic)** | |
| WS | 25 |
| SD | 32 |
| SCK | 33 |
| **MAX98357A (Speaker)**| |
| BCLK | 27 |
| LRC | 26 |
| DOUT | 22 |
| **Button** | 4 |

## 💻 How It Works (Step-by-Step)
1. **Input:** Hold the button and speak into the INMP441 microphone.
2. **Translate:** The ESP32 sends the recorded audio to OpenAI Whisper to transcribe the voice into text.
3. **Process:** The text is sent to ChatGPT, which acts as the assistant and formulates a response.
4. **Speak:** The text response is sent to Google TTS to be converted into an audio format.
5. **Output:** The voice is played aloud through the MAX98357A speaker.

## ⚠️ Setup & Security
**Do not commit API keys or Wi-Fi passwords!** Before running this project, ensure you update the Wi-Fi credentials and insert your own OpenAI API key in the configuration section of the code.
