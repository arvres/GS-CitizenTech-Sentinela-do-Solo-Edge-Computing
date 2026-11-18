<div align="center">

# 🌱 Sentinela do Solo — Estaca Inteligente (Edge Computing)

**Global Solution 2026.2 · CitizenTech · FIAP — Engenharia de Software (1º Ano)**  
*Dispositivo IoT de borda autônomo para monitoramento geotécnico de encostas, prevenção de deslizamentos e alerta comunitário offline/online.*

**Disciplina:** Edge Computing & Computer Systems

[![Simulação Wokwi](https://img.shields.io/badge/Simula%C3%A7%C3%A3o-Wokwi%20ESP32-blue?style=for-the-badge&logo=espressif)](https://wokwi.com/projects/480861247681298433)
[![Vídeo Pitch](https://img.shields.io/badge/V%C3%ADdeo-YouTube-red?style=for-the-badge&logo=youtube)](#)
[![Protocolo](https://img.shields.io/badge/MQTT-HiveMQ-orange?style=for-the-badge&logo=mqtt)](https://www.hivemq.com/)

</div>

---

## 📌 Sobre o Projeto e o Desafio (ODS 11 e ODS 13)

Na região metropolitana de São Paulo, deslizamentos de terra durante tempestades severas ameaçam diretamente famílias que vivem em áreas de encosta. O solo cede silenciosamente, a Defesa Civil não consegue vigiar cada barranco simultaneamente e o alerta ao morador muitas vezes chega tarde ou gera pânico desnecessário.

O **Sentinela do Solo** resolve esse problema por meio de uma **estaca inteligente cravada nos pontos críticos das encostas**. Este repositório contém o firmware e a arquitetura da camada de borda (*Edge Computing*): um microcontrolador **ESP32** que lê continuamente a inclinação do terreno e o volume de chuva, **toma decisões localmente mesmo sem acesso à internet** e emite alertas imediatos no local com LEDs, buzzer e display OLED. Quando há conectividade disponível, publica telemetria estruturada em JSON via MQTT para integração com a Defesa Civil e a comunidade.

---

## 🏗️ Arquitetura em Três Camadas

| Camada | Tecnologia / Componente | Papel no Sistema |
| :--- | :--- | :--- |
| **1. Dispositivo IoT (Edge)** | ESP32 (Simulado no Wokwi) | Lê sensores, aplica filtros matemáticos, calcula derivadas/integrais e decide estados de alerta localmente (operação 100% resiliente offline). |
| **2. Back-end / Mensageria** | Broker MQTT Público (HiveMQ) | Recebe e distribui os pacotes JSON de telemetria em tempo real sem bloquear a execução local. |
| **3. Dashboard / Aplicação** | Aplicação Web React (*Sentinela do Solo*) | Consome os dados do broker e exibe o status da encosta para moradores, Defesa Civil e gestores. |

### 📦 Formato da Telemetria MQTT
Publicada no tópico `gs2026/sentinela-solo/<grupo>/telemetria`:

```json
{
  "id_estaca": "EST-01",
  "cluster_id": "CL-ARGISSOLO-01",
  "angulo_theta": 12.34,
  "taxa": 0.450,
  "chuva_mm_h": 10.0,
  "chuva_acum_mm": 35.2,
  "umidade_solo": 44.0,
  "estado": 1,
  "timestamp": "2026-11-18T10:00:00-03:00"
}
 
