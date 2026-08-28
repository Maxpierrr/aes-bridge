// SPDX-License-Identifier: GPL-3.0-only
mod engine;
mod model;

use engine::{EngineCompatibility, EngineController, EngineStatus, NetworkInterface};
use model::{BridgeConfiguration, ConfigurationProfile, ValidationReport};
use std::fs;
use tauri::Manager;

#[tauri::command]
fn configuration_profiles() -> Vec<ConfigurationProfile> {
    model::profiles()
}

#[tauri::command]
fn validate_configuration(configuration: BridgeConfiguration) -> ValidationReport {
    configuration.validate()
}

#[tauri::command]
fn list_interfaces() -> Result<Vec<NetworkInterface>, String> {
    engine::list_interfaces()
}

#[tauri::command]
fn engine_status() -> EngineStatus {
    engine::status()
}

#[tauri::command]
fn engine_compatibility(configuration: BridgeConfiguration) -> EngineCompatibility {
    engine::compatibility(&configuration)
}

#[tauri::command]
fn start_engine(
    controller: tauri::State<'_, EngineController>,
    configuration: BridgeConfiguration,
) -> Result<String, String> {
    controller.start(&configuration)
}

#[tauri::command]
fn stop_engine(controller: tauri::State<'_, EngineController>) -> Result<String, String> {
    controller.stop()
}

#[tauri::command]
fn restart_engine(
    controller: tauri::State<'_, EngineController>,
    configuration: BridgeConfiguration,
) -> Result<String, String> {
    controller.restart(&configuration)
}

#[tauri::command]
fn save_configuration(
    app: tauri::AppHandle,
    configuration: BridgeConfiguration,
) -> Result<String, String> {
    let validation = configuration.validate();
    if !validation.valid {
        return Err("La configuration contient des erreurs et n’a pas été enregistrée.".into());
    }
    let directory = app
        .path()
        .app_config_dir()
        .map_err(|error| format!("Dossier de configuration inaccessible: {error}"))?;
    fs::create_dir_all(&directory)
        .map_err(|error| format!("Création du dossier impossible: {error}"))?;
    let path = directory.join("configuration.json");
    let bytes = serde_json::to_vec_pretty(&configuration)
        .map_err(|error| format!("Sérialisation impossible: {error}"))?;
    fs::write(&path, bytes).map_err(|error| format!("Écriture impossible: {error}"))?;
    Ok(path.display().to_string())
}

#[tauri::command]
fn load_configuration(app: tauri::AppHandle) -> Result<Option<BridgeConfiguration>, String> {
    let path = app
        .path()
        .app_config_dir()
        .map_err(|error| format!("Dossier de configuration inaccessible: {error}"))?
        .join("configuration.json");
    if !path.exists() {
        return Ok(None);
    }
    let bytes = fs::read(&path).map_err(|error| format!("Lecture impossible: {error}"))?;
    let configuration = serde_json::from_slice(&bytes)
        .map_err(|error| format!("Configuration invalide: {error}"))?;
    Ok(Some(configuration))
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let app = tauri::Builder::default()
        .manage(EngineController::default())
        .invoke_handler(tauri::generate_handler![
            configuration_profiles,
            validate_configuration,
            list_interfaces,
            engine_status,
            engine_compatibility,
            start_engine,
            stop_engine,
            restart_engine,
            save_configuration,
            load_configuration
        ])
        .build(tauri::generate_context!())
        .expect("AES Bridge control application failed");
    app.run(|app_handle, event| {
        if matches!(
            event,
            tauri::RunEvent::ExitRequested { .. } | tauri::RunEvent::Exit
        ) {
            let _ = app_handle.state::<EngineController>().stop();
        }
    });
}
