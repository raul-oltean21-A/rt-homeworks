package ssatr.device;

import java.util.concurrent.BlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 *
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 *
 *   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 *   SET_THRESHOLD <valoare> schimbă pragul de alarmă
 *   RESET                   readuce senzorul la valoarea inițială
 *   STATUS                  raportează perioada și pragul curente
 *
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        while (!Thread.currentThread().isInterrupted()) {
            try {
                String cmd = commands.take();
                long timestamp = System.currentTimeMillis();

                String[] parts = cmd.split(" ");
                String action = parts[0];
                String responseText;

                try {
                    switch (action) {
                        case "SET_INTERVAL":
                            long newInterval = Long.parseLong(parts[1]);
                            periodic.setPeriodMs(newInterval);
                            responseText = "OK " + cmd;
                            break;
                        case "SET_THRESHOLD":
                            double newThreshold = Double.parseDouble(parts[1]);
                            periodic.setThreshold(newThreshold);
                            responseText = "OK " + cmd;
                            break;
                        case "RESET":
                            sensor.reset();
                            responseText = "OK " + cmd;
                            break;
                        case "STATUS":
                            responseText = String.format(java.util.Locale.US, "OK STATUS T=%d Prag=%.2f",
                                    periodic.getPeriodMs(), periodic.getThreshold());
                            break;
                        default:
                            responseText = "ERROR Comanda necunoscuta: " + action;
                            break;
                    }
                } catch (Exception e) {
                    responseText = "ERROR Parametri invalizi pentru comanda: " + cmd;
                }

                String payload = String.format(java.util.Locale.US, "%s;%d;%s", deviceId, timestamp, responseText);
                mqtt.publish(Config.statusTopic(deviceId), payload);

            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
    }
}
