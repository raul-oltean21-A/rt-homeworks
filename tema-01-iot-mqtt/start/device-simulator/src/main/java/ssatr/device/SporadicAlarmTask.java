package ssatr.device;

import java.util.concurrent.BlockingQueue;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 *
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;

    public SporadicAlarmTask(String deviceId, MqttConnection mqtt, BlockingQueue<Double> alarmEvents) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    @Override
    public void run() {
        long lastReleaseTime = 0;

        while (!Thread.currentThread().isInterrupted()) {
            try {
                Double temp = alarmEvents.take();
                long currentTime = System.currentTimeMillis();

                if (currentTime - lastReleaseTime >= MIN_INTERARRIVAL_MS) {

                    String payload = String.format(java.util.Locale.US, "%s;%d;%.2f;28.0",
                            deviceId, currentTime, temp);

                    mqtt.publish(Config.alarmTopic(deviceId), payload);
                    lastReleaseTime = currentTime; // Actualizăm timpul ultimei eliberări
                } else {
                    System.out.println("[" + deviceId + "] Alarma respinsă: a sosit prea repede (interval < 5s).");
                }

            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
    }
}
