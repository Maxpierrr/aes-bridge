// SPDX-License-Identifier: GPL-3.0-only
use crate::model::{BridgeConfiguration, FlowConfiguration, FlowDirection};
use serde::Serialize;
use serde_json::Value;
use std::env;
use std::fs::OpenOptions;
use std::net::Ipv4Addr;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use std::thread;
use std::time::{Duration, Instant};

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NetworkInterface {
    pub name: String,
    pub address: String,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EngineStatus {
    pub available: bool,
    pub running: bool,
    pub executable: Option<String>,
    pub status: Option<Value>,
    pub message: String,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EngineCompatibility {
    pub supported: bool,
    pub message: String,
}

#[derive(Default)]
pub struct EngineController {
    child: Mutex<Option<Child>>,
}

fn executable_name() -> &'static str {
    if cfg!(windows) {
        "aes-bridge-windows-backend.exe"
    } else {
        "aes-bridge-engine"
    }
}

fn candidates() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Ok(path) = env::var("AES_BRIDGE_ENGINE_PATH") {
        paths.push(PathBuf::from(path));
    }
    if let Ok(current) = env::current_exe()
        && let Some(parent) = current.parent()
    {
        paths.push(parent.join(executable_name()));
        paths.push(parent.join("../Resources").join(executable_name()));
    }
    let repository = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    paths.push(PathBuf::from("/private/tmp/aes-bridge-cmake-build").join(executable_name()));
    paths.push(repository.join("build").join(executable_name()));
    paths.push(PathBuf::from("/private/tmp/aes-bridge-portable-build").join(executable_name()));
    if cfg!(target_os = "macos") {
        paths.push(PathBuf::from(
            "/Applications/AES Bridge.app/Contents/Resources/aes-bridge-engine",
        ));
    }
    paths
}

fn locate() -> Option<PathBuf> {
    candidates().into_iter().find(|path| path.is_file())
}

pub fn list_interfaces() -> Result<Vec<NetworkInterface>, String> {
    let executable = locate().ok_or_else(|| "Moteur AES Bridge introuvable.".to_string())?;
    let output = Command::new(&executable)
        .arg("--list-interfaces")
        .output()
        .map_err(|error| format!("Impossible d’exécuter {}: {error}", executable.display()))?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).trim().to_string());
    }
    let mut interfaces: Vec<NetworkInterface> = String::from_utf8_lossy(&output.stdout)
        .lines()
        .filter_map(|line| {
            let (name, address) = line.split_once('\t')?;
            Some(NetworkInterface {
                name: name.into(),
                address: address.into(),
            })
        })
        .collect();
    if !interfaces.iter().any(|interface| interface.name == "lo0") {
        interfaces.push(NetworkInterface {
            name: "lo0".into(),
            address: "127.0.0.1".into(),
        });
    }
    Ok(interfaces)
}

pub fn status() -> EngineStatus {
    let Some(executable) = locate() else {
        return EngineStatus {
            available: false,
            running: false,
            executable: None,
            status: None,
            message: "Moteur AES Bridge introuvable.".into(),
        };
    };
    let executable_text = executable.display().to_string();
    match Command::new(&executable).arg("--status").output() {
        Ok(output) if output.status.success() => {
            match serde_json::from_slice::<Value>(&output.stdout) {
                Ok(status) => {
                    let running = status
                        .get("engineRunning")
                        .and_then(Value::as_bool)
                        .unwrap_or(true);
                    EngineStatus {
                        available: true,
                        running,
                        executable: Some(executable_text),
                        status: running.then_some(status),
                        message: if running {
                            "Moteur actif.".into()
                        } else {
                            "Moteur arrêté.".into()
                        },
                    }
                }
                Err(error) => EngineStatus {
                    available: true,
                    running: false,
                    executable: Some(executable_text),
                    status: None,
                    message: format!("État du moteur illisible: {error}"),
                },
            }
        }
        Ok(output) => EngineStatus {
            available: true,
            running: false,
            executable: Some(executable_text),
            status: None,
            message: String::from_utf8_lossy(&output.stderr).trim().to_string(),
        },
        Err(error) => EngineStatus {
            available: true,
            running: false,
            executable: Some(executable_text),
            status: None,
            message: format!("Impossible d’interroger le moteur: {error}"),
        },
    }
}

pub fn compatibility(configuration: &BridgeConfiguration) -> EngineCompatibility {
    match arguments_for(configuration) {
        Ok(_) => EngineCompatibility {
            supported: true,
            message: "Configuration exécutable par le moteur actuel.".into(),
        },
        Err(message) => EngineCompatibility {
            supported: false,
            message,
        },
    }
}

impl EngineController {
    pub fn start(&self, configuration: &BridgeConfiguration) -> Result<String, String> {
        if status().running {
            return Err("Le moteur AES Bridge est déjà actif.".into());
        }
        {
            let mut guard = self
                .child
                .lock()
                .map_err(|_| "État du moteur inaccessible.")?;
            if let Some(mut previous) = guard.take() {
                match previous.try_wait() {
                    Ok(Some(_)) => {}
                    Ok(None) => {
                        *guard = Some(previous);
                        return Err("Un processus moteur est encore en cours de fermeture.".into());
                    }
                    Err(error) => {
                        return Err(format!("Ancien processus moteur inaccessible: {error}"));
                    }
                }
            }
        }
        let executable = locate().ok_or_else(|| "Moteur AES Bridge introuvable.".to_string())?;
        let arguments = arguments_for(configuration)?;
        let log_path = env::temp_dir().join("aes-bridge-engine.log");
        let stdout = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&log_path)
            .map_err(|error| format!("Journal moteur inaccessible: {error}"))?;
        let stderr = stdout
            .try_clone()
            .map_err(|error| format!("Journal moteur inaccessible: {error}"))?;
        let child = Command::new(&executable)
            .args(arguments)
            .stdin(Stdio::null())
            .stdout(Stdio::from(stdout))
            .stderr(Stdio::from(stderr))
            .spawn()
            .map_err(|error| format!("Démarrage du moteur impossible: {error}"))?;
        let pid = child.id();
        *self
            .child
            .lock()
            .map_err(|_| "État du moteur inaccessible.")? = Some(child);

        for _ in 0..30 {
            if status().running {
                return Ok(format!(
                    "Moteur démarré (PID {pid}). Journal: {}",
                    log_path.display()
                ));
            }
            thread::sleep(Duration::from_millis(50));
        }
        let _ = self.stop();
        Err(format!(
            "Le moteur n’a pas confirmé son démarrage. Consultez {}.",
            log_path.display()
        ))
    }

    pub fn stop(&self) -> Result<String, String> {
        let mut guard = self
            .child
            .lock()
            .map_err(|_| "État du moteur inaccessible.")?;
        let Some(mut child) = guard.take() else {
            return if status().running {
                Err("Le moteur actif n’a pas été lancé par cette instance de l’application.".into())
            } else {
                Ok("Le moteur est déjà arrêté.".into())
            };
        };

        #[cfg(unix)]
        {
            let _ = Command::new("/bin/kill")
                .args(["-TERM", &child.id().to_string()])
                .status();
        }
        #[cfg(windows)]
        let _ = child.kill();

        let deadline = Instant::now() + Duration::from_secs(2);
        while Instant::now() < deadline {
            match child.try_wait() {
                Ok(Some(_)) => return Ok("Moteur arrêté proprement.".into()),
                Ok(None) => thread::sleep(Duration::from_millis(50)),
                Err(error) => return Err(format!("Arrêt du moteur impossible: {error}")),
            }
        }
        child
            .kill()
            .map_err(|error| format!("Arrêt forcé du moteur impossible: {error}"))?;
        let _ = child.wait();
        Ok("Moteur arrêté après expiration du délai de fermeture.".into())
    }

    pub fn restart(&self, configuration: &BridgeConfiguration) -> Result<String, String> {
        self.stop()?;
        self.start(configuration)
    }
}

fn arguments_for(configuration: &BridgeConfiguration) -> Result<Vec<String>, String> {
    if !configuration.validate().valid {
        return Err("La configuration contient des erreurs.".into());
    }
    if configuration.interface_name.is_empty() && configuration.interface_address.is_empty() {
        return Err("Choisissez explicitement une interface Ethernet.".into());
    }
    let mut receive = enabled_flows(configuration, FlowDirection::Receive);
    let mut transmit = enabled_flows(configuration, FlowDirection::Transmit);
    receive.sort_by_key(|flow| flow.core_audio_start_channel);
    transmit.sort_by_key(|flow| flow.core_audio_start_channel);
    if receive.is_empty() || receive.len() != transmit.len() || receive.len() > 8 {
        return Err("Le moteur actuel exige le même nombre de flux RX et TX (1 à 8).".into());
    }
    validate_banks(&receive, "RX")?;
    validate_banks(&transmit, "TX")?;
    if receive[0].channels != transmit[0].channels
        || receive[0].core_audio_start_channel != transmit[0].core_audio_start_channel
    {
        return Err("Le moteur actuel exige le même nombre de canaux et le même départ Core Audio en RX et TX.".into());
    }
    let rx_stride = port_stride(&receive)?;
    let tx_stride = port_stride(&transmit)?;
    if rx_stride != tx_stride {
        return Err("Le moteur actuel exige le même pas de port pour RX et TX.".into());
    }
    let jitter = receive[0].jitter_packets;
    if receive
        .iter()
        .chain(transmit.iter())
        .any(|flow| flow.jitter_packets != jitter)
    {
        return Err(
            "Le moteur actuel utilise une seule valeur de tampon pour tous les flux.".into(),
        );
    }
    let source = &receive[0].source_address;
    if receive.iter().any(|flow| &flow.source_address != source) {
        return Err("Le moteur actuel utilise un filtre de source RX commun.".into());
    }

    let mut args = vec!["--run".into()];
    args.extend(["--parent-pid".into(), std::process::id().to_string()]);
    if !configuration.interface_name.is_empty() {
        args.extend(["--interface".into(), configuration.interface_name.clone()]);
    }
    if !configuration.interface_address.is_empty() {
        args.extend([
            "--interface-address".into(),
            configuration.interface_address.clone(),
        ]);
    }
    args.extend([
        "--profile".into(),
        "raspberry".into(),
        "--stream-count".into(),
        receive.len().to_string(),
        "--channels-per-stream".into(),
        receive[0].channels.to_string(),
        "--core-audio-start-channel".into(),
        receive[0].core_audio_start_channel.to_string(),
        "--rx-group".into(),
        receive[0].multicast_address.clone(),
        "--tx-group".into(),
        transmit[0].multicast_address.clone(),
        "--rx-port".into(),
        receive[0].port.to_string(),
        "--tx-port".into(),
        transmit[0].port.to_string(),
        "--port-stride".into(),
        rx_stride.to_string(),
        "--rx-payload-type".into(),
        receive[0].payload_type.to_string(),
        "--tx-payload-type".into(),
        transmit[0].payload_type.to_string(),
        "--jitter-packets".into(),
        jitter.to_string(),
    ]);
    if !source.is_empty() {
        args.extend(["--rx-source".into(), source.clone()]);
    }
    if !configuration.sap_discovery {
        args.push("--no-sap-discovery".into());
    }
    if !configuration.sap_publication {
        args.push("--no-sap-publish".into());
    }
    if !configuration.ptp_enabled {
        args.push("--no-ptp".into());
    }
    Ok(args)
}

fn enabled_flows(
    configuration: &BridgeConfiguration,
    direction: FlowDirection,
) -> Vec<&FlowConfiguration> {
    configuration
        .flows
        .iter()
        .filter(|flow| flow.enabled && flow.direction == direction)
        .collect()
}

fn validate_banks(flows: &[&FlowConfiguration], label: &str) -> Result<(), String> {
    let payload_type = flows[0].payload_type;
    let channels = flows[0].channels;
    let first_core_audio_channel = flows[0].core_audio_start_channel;
    let base_address = address_number(&flows[0].multicast_address)?;
    for (index, flow) in flows.iter().enumerate() {
        let expected_channel = first_core_audio_channel + index as u16 * channels;
        if flow.channels != channels || flow.core_audio_start_channel != expected_channel {
            return Err(format!(
                "Les flux {label} doivent avoir la même largeur et occuper des canaux Core Audio consécutifs."
            ));
        }
        if flow.payload_type != payload_type {
            return Err(format!(
                "Tous les flux {label} doivent partager le même payload RTP."
            ));
        }
        let expected_address = base_address
            .checked_add(index as u32)
            .ok_or_else(|| format!("Plage multicast {label} hors IPv4."))?;
        if address_number(&flow.multicast_address)? != expected_address {
            return Err(format!(
                "Les adresses multicast {label} doivent être consécutives."
            ));
        }
    }
    Ok(())
}

fn address_number(address: &str) -> Result<u32, String> {
    address
        .parse::<Ipv4Addr>()
        .map(u32::from)
        .map_err(|_| format!("Adresse IPv4 invalide: {address}"))
}

fn port_stride(flows: &[&FlowConfiguration]) -> Result<u16, String> {
    if flows.len() == 1 {
        return Ok(0);
    }
    let stride = flows[1]
        .port
        .checked_sub(flows[0].port)
        .ok_or_else(|| "Les ports RTP doivent être croissants.".to_string())?;
    if flows.iter().enumerate().any(|(index, flow)| {
        flow.port
            != flows[0]
                .port
                .saturating_add(stride.saturating_mul(index as u16))
    }) {
        return Err("Les ports RTP doivent suivre un pas constant.".into());
    }
    Ok(stride)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::profiles;

    #[test]
    fn bundled_uniform_profiles_map_to_current_engine() {
        for profile_id in ["raspberry", "computer-a", "planet22c-x4"] {
            let mut configuration = profiles()
                .into_iter()
                .find(|profile| profile.id == profile_id)
                .unwrap()
                .configuration;
            configuration.interface_address = "127.0.0.1".into();
            assert!(arguments_for(&configuration).is_ok());
        }
    }

    #[test]
    fn stereo_planet_profile_maps_to_two_channel_engine() {
        let mut configuration = profiles()
            .into_iter()
            .find(|profile| profile.id == "planet22c")
            .unwrap()
            .configuration;
        configuration.interface_address = "127.0.0.1".into();
        let arguments = arguments_for(&configuration).unwrap();
        assert!(
            arguments
                .windows(2)
                .any(|pair| pair == ["--channels-per-stream", "2"])
        );
        assert!(
            arguments
                .windows(2)
                .any(|pair| pair == ["--core-audio-start-channel", "1"])
        );
    }

    #[test]
    fn loopback_profile_is_safe_and_launchable() {
        let configuration = profiles()
            .into_iter()
            .find(|profile| profile.id == "diagnostic-loopback")
            .unwrap()
            .configuration;
        let arguments = arguments_for(&configuration).unwrap();
        assert!(
            arguments
                .windows(2)
                .any(|pair| pair == ["--interface-address", "127.0.0.1"])
        );
        assert!(
            arguments
                .iter()
                .any(|argument| argument == "--no-sap-discovery")
        );
        assert!(
            arguments
                .iter()
                .any(|argument| argument == "--no-sap-publish")
        );
        assert!(arguments.iter().any(|argument| argument == "--no-ptp"));
    }
}
