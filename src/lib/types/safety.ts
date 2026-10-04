export type WatchStatus = 'connected' | 'disconnected';

export type AlertPolicy = 'contacts_only' | 'confirm_before_send';

export type SafetyPlan = {
    watchStatus: WatchStatus;
    betteryPercent: number;
    monitoringEnabled: boolean;
    noResponsePolicy: AlertPolicy;
    includeLocation: boolean;
};

export type TrustedContact = {
    id: string;
    occurredAt: string;
    kind: 'watch_checkin' | 'cancelled' | 'alert_previewed' | 'watch_paired';
    description: string;
};
