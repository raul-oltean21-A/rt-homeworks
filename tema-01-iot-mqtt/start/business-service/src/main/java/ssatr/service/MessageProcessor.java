package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        while (!Thread.currentThread().isInterrupted()) {
            try {
                String mesaj = inbox.take();

                storage.append(kind, mesaj);

                String[] parts = mesaj.split(";");
                if (parts.length < 3) continue;

                String deviceId = parts[0];
                long timestamp = Long.parseLong(parts[1]);

                if (kind.equals("telemetry")) {
                    double temp = Double.parseDouble(parts[2]);
                    dashboard.updateTelemetry(deviceId, temp, timestamp);

                } else if (kind.equals("alarm")) {
                    dashboard.incrementAlarm(deviceId, timestamp);

                    System.out.println("\n[ALARMĂ NOUĂ] " + mesaj + "\n> ");

                } else if (kind.equals("status")) {
                    dashboard.updateStatus(deviceId, timestamp);
                    System.out.println("\n[STATUS] " + mesaj + "\n> ");
                }

            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            } catch (Exception e) {
                System.err.println("Eroare la procesarea mesajului (" + kind + "): " + e.getMessage());
            }
        }
    }
}
