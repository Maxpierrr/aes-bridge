// SPDX-License-Identifier: GPL-3.0-only
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::net::Ipv4Addr;

const VIRTUAL_CHANNELS: u16 = 64;
const SAMPLE_RATE: u32 = 48_000;
const FRAMES_PER_PACKET: u16 = 48;
const PACKET_TIME_MICROSECONDS: u32 = 1_000;
const MAX_FLOWS_PER_DIRECTION: usize = 32;

#[derive(Clone, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum FlowDirection {
    Receive,
    Transmit,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FlowConfiguration {
    pub id: String,
    pub name: String,
    pub enabled: bool,
    pub direction: FlowDirection,
    pub multicast_address: String,
    #[serde(default)]
    pub source_address: String,
    pub port: u16,
    pub payload_type: u8,
    pub encoding: String,
    pub sample_rate: u32,
    pub channels: u16,
    pub frames_per_packet: u16,
    pub packet_time_microseconds: u32,
    pub core_audio_start_channel: u16,
    pub jitter_packets: u16,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BridgeConfiguration {
    pub schema_version: u32,
    pub name: String,
    #[serde(default)]
    pub profile_id: String,
    #[serde(default)]
    pub interface_name: String,
    #[serde(default)]
    pub interface_address: String,
    pub ptp_enabled: bool,
    pub ptp_domain: u8,
    pub sap_discovery: bool,
    pub sap_publication: bool,
    pub flows: Vec<FlowConfiguration>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidationIssue {
    pub field: String,
    pub message: String,
    pub flow_id: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidationReport {
    pub valid: bool,
    pub issues: Vec<ValidationIssue>,
    pub receive_channels: u16,
    pub transmit_channels: u16,
    pub receive_flows: usize,
    pub transmit_flows: usize,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConfigurationProfile {
    pub id: String,
    pub title: String,
    pub description: String,
    pub configuration: BridgeConfiguration,
}

fn flow(
    id: impl Into<String>,
    name: impl Into<String>,
    direction: FlowDirection,
    multicast_address: impl Into<String>,
    channels: u16,
    core_audio_start_channel: u16,
) -> FlowConfiguration {
    FlowConfiguration {
        id: id.into(),
        name: name.into(),
        enabled: true,
        direction,
        multicast_address: multicast_address.into(),
        source_address: String::new(),
        port: 5004,
        payload_type: 96,
        encoding: "L24".into(),
        sample_rate: SAMPLE_RATE,
        channels,
        frames_per_packet: FRAMES_PER_PACKET,
        packet_time_microseconds: PACKET_TIME_MICROSECONDS,
        core_audio_start_channel,
        jitter_packets: 6,
    }
}

fn base_configuration(id: &str, name: &str, flows: Vec<FlowConfiguration>) -> BridgeConfiguration {
    BridgeConfiguration {
        schema_version: 1,
        name: name.into(),
        profile_id: id.into(),
        interface_name: String::new(),
        interface_address: String::new(),
        ptp_enabled: true,
        ptp_domain: 0,
        sap_discovery: true,
        sap_publication: true,
        flows,
    }
}

pub fn profiles() -> Vec<ConfigurationProfile> {
    let raspberry = base_configuration(
        "raspberry",
        "Raspberry Pi 8×8",
        vec![
            flow(
                "pi-rx",
                "RASPIAUDIO IN 1–8",
                FlowDirection::Receive,
                "239.69.83.80",
                8,
                1,
            ),
            flow(
                "pi-tx",
                "RASPIAUDIO OUT 1–8",
                FlowDirection::Transmit,
                "239.69.83.81",
                8,
                1,
            ),
        ],
    );

    let planet = base_configuration(
        "planet22c",
        "planet 22c stéréo",
        vec![
            flow(
                "planet-1-rx",
                "planet 1 IN 1–2",
                FlowDirection::Receive,
                "239.69.83.82",
                2,
                1,
            ),
            flow(
                "planet-1-tx",
                "planet 1 OUT 1–2",
                FlowDirection::Transmit,
                "239.69.83.83",
                2,
                1,
            ),
        ],
    );

    let mut planet_x4_flows = Vec::new();
    for index in 0..4u16 {
        let first_channel = index * 2 + 1;
        planet_x4_flows.push(flow(
            format!("planet-{}-rx", index + 1),
            format!("planet {} IN 1–2", index + 1),
            FlowDirection::Receive,
            format!("239.69.83.{}", 82 + index * 2),
            2,
            first_channel,
        ));
        planet_x4_flows.push(flow(
            format!("planet-{}-tx", index + 1),
            format!("planet {} OUT 1–2", index + 1),
            FlowDirection::Transmit,
            format!("239.69.83.{}", 83 + index * 2),
            2,
            first_channel,
        ));
    }
    let planet_x4 = base_configuration("planet22c-x4", "Quatre planet 22c · 8×8", planet_x4_flows);

    let mut computer_a_flows = Vec::new();
    for index in 0..8u16 {
        let first_channel = index * 8 + 1;
        computer_a_flows.push(flow(
            format!("computer-rx-{index}"),
            format!(
                "Ordinateur distant RX {}–{}",
                first_channel,
                first_channel + 7
            ),
            FlowDirection::Receive,
            format!("239.69.83.{}", 80 + index),
            8,
            first_channel,
        ));
        computer_a_flows.push(flow(
            format!("computer-tx-{index}"),
            format!("AES Bridge TX {}–{}", first_channel, first_channel + 7),
            FlowDirection::Transmit,
            format!("239.69.83.{}", 96 + index),
            8,
            first_channel,
        ));
    }
    let computer_a = base_configuration("computer-a", "Ordinateur A · 64×64", computer_a_flows);

    let mut diagnostic = base_configuration(
        "diagnostic-loopback",
        "Diagnostic boucle locale 8×8",
        vec![
            flow(
                "loopback-rx",
                "Boucle locale RX 1–8",
                FlowDirection::Receive,
                "127.0.0.1",
                8,
                1,
            ),
            flow(
                "loopback-tx",
                "Boucle locale TX 1–8",
                FlowDirection::Transmit,
                "127.0.0.1",
                8,
                1,
            ),
        ],
    );
    diagnostic.interface_name = "lo0".into();
    diagnostic.interface_address = "127.0.0.1".into();
    diagnostic.ptp_enabled = false;
    diagnostic.sap_discovery = false;
    diagnostic.sap_publication = false;
    for flow in &mut diagnostic.flows {
        flow.port = 55_120;
        flow.jitter_packets = 3;
    }

    vec![
        ConfigurationProfile {
            id: "raspberry".into(),
            title: "Raspberry Pi 8×8".into(),
            description: "Une paire de flux huit canaux compatible avec LXToolPi/RASPIAUDIO.".into(),
            configuration: raspberry,
        },
        ConfigurationProfile {
            id: "planet22c".into(),
            title: "planet 22c".into(),
            description: "Une paire stéréo AES67. Les adresses devront correspondre aux flux créés dans Dante Controller.".into(),
            configuration: planet,
        },
        ConfigurationProfile {
            id: "planet22c-x4".into(),
            title: "4 × planet 22c".into(),
            description: "Quatre paires stéréo routées vers les canaux Core Audio 1 à 8.".into(),
            configuration: planet_x4,
        },
        ConfigurationProfile {
            id: "computer-a".into(),
            title: "Ordinateur A 64×64".into(),
            description: "Huit flux de huit canaux dans chaque direction.".into(),
            configuration: computer_a,
        },
        ConfigurationProfile {
            id: "diagnostic-loopback".into(),
            title: "Diagnostic local 8×8".into(),
            description: "Test RTP sur le Mac uniquement, sans trafic sur le réseau Ethernet.".into(),
            configuration: diagnostic,
        },
    ]
}

impl BridgeConfiguration {
    pub fn validate(&self) -> ValidationReport {
        let mut issues = Vec::new();
        let mut ids = HashSet::new();
        let mut occupied: HashMap<&FlowDirection, [Option<&str>; VIRTUAL_CHANNELS as usize]> =
            HashMap::new();
        let mut receive_channels = 0;
        let mut transmit_channels = 0;
        let mut receive_flows = 0;
        let mut transmit_flows = 0;

        if self.schema_version != 1 {
            issues.push(issue(
                "schemaVersion",
                "Version de schéma non prise en charge.",
                None,
            ));
        }
        if self.name.trim().is_empty() {
            issues.push(issue(
                "name",
                "Le nom de la configuration est obligatoire.",
                None,
            ));
        }
        if self.ptp_enabled && self.ptp_domain != 0 {
            issues.push(issue(
                "ptpDomain",
                "AES67 doit utiliser le domaine PTPv2 0.",
                None,
            ));
        }

        for flow in self.flows.iter().filter(|flow| flow.enabled) {
            let flow_id = Some(flow.id.clone());
            if flow.id.trim().is_empty() || !ids.insert(flow.id.as_str()) {
                issues.push(issue(
                    "id",
                    "Identifiant de flux vide ou dupliqué.",
                    flow_id.clone(),
                ));
            }
            if flow.name.trim().is_empty() {
                issues.push(issue(
                    "name",
                    "Le nom du flux est obligatoire.",
                    flow_id.clone(),
                ));
            }
            let multicast = flow.multicast_address.parse::<Ipv4Addr>();
            let diagnostic_loopback = self.profile_id == "diagnostic-loopback"
                && matches!(multicast, Ok(address) if address.is_loopback());
            if !diagnostic_loopback && !matches!(multicast, Ok(address) if address.is_multicast()) {
                issues.push(issue(
                    "multicastAddress",
                    "Adresse multicast IPv4 invalide.",
                    flow_id.clone(),
                ));
            }
            if !flow.source_address.is_empty() && flow.source_address.parse::<Ipv4Addr>().is_err() {
                issues.push(issue(
                    "sourceAddress",
                    "Filtre de source IPv4 invalide.",
                    flow_id.clone(),
                ));
            }
            if flow.port == 0 {
                issues.push(issue(
                    "port",
                    "Le port RTP ne peut pas être zéro.",
                    flow_id.clone(),
                ));
            }
            if flow.payload_type > 127 {
                issues.push(issue(
                    "payloadType",
                    "Le payload RTP doit être compris entre 0 et 127.",
                    flow_id.clone(),
                ));
            }
            if flow.encoding != "L24" {
                issues.push(issue(
                    "encoding",
                    "Seul l’encodage L24 est pris en charge dans cette version.",
                    flow_id.clone(),
                ));
            }
            if flow.sample_rate != SAMPLE_RATE {
                issues.push(issue(
                    "sampleRate",
                    "Seule la fréquence 48 kHz est prise en charge.",
                    flow_id.clone(),
                ));
            }
            if !matches!(flow.channels, 1 | 2 | 4 | 8) {
                issues.push(issue(
                    "channels",
                    "Un flux doit contenir 1, 2, 4 ou 8 canaux.",
                    flow_id.clone(),
                ));
            }
            if flow.frames_per_packet != FRAMES_PER_PACKET
                || flow.packet_time_microseconds != PACKET_TIME_MICROSECONDS
            {
                issues.push(issue(
                    "packetTime",
                    "Le paquet doit contenir 48 trames / 1 ms.",
                    flow_id.clone(),
                ));
            }
            if !(2..=63).contains(&flow.jitter_packets) {
                issues.push(issue(
                    "jitterPackets",
                    "Le tampon anti-gigue doit contenir 2 à 63 paquets.",
                    flow_id.clone(),
                ));
            }
            let last_channel = flow
                .core_audio_start_channel
                .saturating_add(flow.channels.saturating_sub(1));
            if flow.core_audio_start_channel == 0 || last_channel > VIRTUAL_CHANNELS {
                issues.push(issue(
                    "coreAudioStartChannel",
                    "Le routage dépasse les 64 canaux Core Audio.",
                    flow_id.clone(),
                ));
            } else {
                let channels = occupied
                    .entry(&flow.direction)
                    .or_insert([None; VIRTUAL_CHANNELS as usize]);
                for index in (flow.core_audio_start_channel - 1)..last_channel {
                    let slot = &mut channels[index as usize];
                    if let Some(previous) = slot {
                        issues.push(issue(
                            "coreAudioStartChannel",
                            format!("Chevauchement de routage avec le flux {previous}."),
                            flow_id.clone(),
                        ));
                        break;
                    }
                    *slot = Some(flow.id.as_str());
                }
            }
            match flow.direction {
                FlowDirection::Receive => {
                    receive_flows += 1;
                    receive_channels += flow.channels;
                }
                FlowDirection::Transmit => {
                    transmit_flows += 1;
                    transmit_channels += flow.channels;
                }
            }
        }

        if receive_flows > MAX_FLOWS_PER_DIRECTION || transmit_flows > MAX_FLOWS_PER_DIRECTION {
            issues.push(issue("flows", "Maximum 32 flux par direction.", None));
        }

        ValidationReport {
            valid: issues.is_empty(),
            issues,
            receive_channels,
            transmit_channels,
            receive_flows,
            transmit_flows,
        }
    }
}

fn issue(
    field: impl Into<String>,
    message: impl Into<String>,
    flow_id: Option<String>,
) -> ValidationIssue {
    ValidationIssue {
        field: field.into(),
        message: message.into(),
        flow_id,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bundled_profiles_are_valid() {
        for profile in profiles() {
            let report = profile.configuration.validate();
            assert!(report.valid, "{}: {:?}", profile.id, report.issues);
        }
    }

    #[test]
    fn rejects_overlapping_routes_and_unsupported_channel_counts() {
        let mut configuration = profiles().remove(0).configuration;
        configuration.flows[1].direction = FlowDirection::Receive;
        configuration.flows[1].channels = 3;
        let report = configuration.validate();
        assert!(!report.valid);
        assert!(report.issues.iter().any(|issue| issue.field == "channels"));
        assert!(
            report
                .issues
                .iter()
                .any(|issue| issue.message.contains("Chevauchement"))
        );
    }

    #[test]
    fn planet_profile_maps_stereo_without_exceeding_core_audio() {
        let profile = profiles()
            .into_iter()
            .find(|profile| profile.id == "planet22c-x4")
            .unwrap();
        let report = profile.configuration.validate();
        assert_eq!(report.receive_channels, 8);
        assert_eq!(report.transmit_channels, 8);
        assert_eq!(report.receive_flows, 4);
        assert_eq!(report.transmit_flows, 4);
    }
}
