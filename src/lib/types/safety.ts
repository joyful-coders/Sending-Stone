export type WatchStatus = 'connected' | 'disconnected';

export type AlertPolicy = 'contacts_only' | 'confirm_before_send';

export type SafetyPlan = {
    id: number;
    checkInEnabled: boolean;
    checkInInterval: string;
    gracePeriod: string;
    alertMethod: string;
    includedLocation: boolean;
    updatedAt: string;
};

export type TrustedContact = {
    id: number;
    name: string;
    relationship: string;
    phone: string;
    createdAt: string;
};

export type ActivityEvent = {
    id: number;
    title: string;
    detail: string;
    createdAt: string;
};
