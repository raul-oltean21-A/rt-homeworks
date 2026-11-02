package ssatr.service;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class Dashboard {

    private static class DeviceState {
        double lastTemp = 0.0;
        long lastMessageTime = 0;
        int alarmCount = 0;
    }

    private final Map<String, DeviceState> states = new ConcurrentHashMap<>();

    public void updateTelemetry(String deviceId, double temp, long timestamp) {
        DeviceState state = states.computeIfAbsent(deviceId, k -> new DeviceState());
        state.lastTemp = temp;
        state.lastMessageTime = timestamp;
    }

    public void incrementAlarm(String deviceId, long timestamp) {
        DeviceState state = states.computeIfAbsent(deviceId, k -> new DeviceState());
        state.alarmCount++;
        state.lastMessageTime = timestamp;
    }

    public void updateStatus(String deviceId, long timestamp) {
        DeviceState state = states.computeIfAbsent(deviceId, k -> new DeviceState());
        state.lastMessageTime = timestamp;
    }

    /** Afișează un tabel cu starea tuturor device-urilor. */
    public void print() {
        System.out.println("---------------------------------------------------------------");
        System.out.printf("%-15s %-10s %-20s %-10s%n", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME");
        System.out.println("---------------------------------------------------------------");

        long now = System.currentTimeMillis();

        for (Map.Entry<String, DeviceState> entry : states.entrySet()) {
            DeviceState s = entry.getValue();
            long secondsAgo = (now - s.lastMessageTime) / 1000;
            System.out.printf("%-15s %-10.2f acum %-15d %-10d%n",
                    entry.getKey(), s.lastTemp, secondsAgo, s.alarmCount);
        }
        System.out.println("---------------------------------------------------------------");
    }
}