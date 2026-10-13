# Event-Triggered Industrial Quality Inspection System 🔍🤖

This repository contains the prototype architecture and implementation of an automated, event-triggered industrial quality inspection system. The system utilizes computer vision and deep learning to identify product types and evaluate samples (PASS/FAIL) in an automated workflow.

## 🌟 Key Features
- **Deep Learning-Based Inference:** Utilizes pretrained `ResNet-18` embeddings (via PyTorch) for product identification.
- **Anomaly Detection:** Implements a PatchCore-style, GOOD-only anomaly detector for evaluating samples.
- **Workflow Automation:** Features Region of Interest (ROI) monitoring and product-identification gating.
- **Data Persistence:** Persistent inspection records and annotated image storage managed via `SQLite`.
- **Hardware Integration:** Includes a lightweight serial Human-Machine Interface (HMI) using an `Arduino UNO` and an `ST7735S` display.

## 🛠️ Technology Stack
*   **Software & AI:** Python, OpenCV, PyTorch / torchvision, scikit-learn
*   **Database:** SQLite
*   **Embedded Hardware:** Arduino UNO, ST7735S Display module

## 📂 Project Structure
*   `/src`: Python scripts for vision-based inference, camera integration, and database management.
*   `/arduino_hmi`: Arduino `.ino` files for serial communication and display updating.
*   `/docs`: System architecture diagrams and project reports (Note: Quantitative benchmark data is omitted as this is a functional prototype).

## ⚖️ Academic Context
*This project was developed as a Capstone Project to demonstrate the integration of embedded computing with deep learning for civilian industrial automation.*
