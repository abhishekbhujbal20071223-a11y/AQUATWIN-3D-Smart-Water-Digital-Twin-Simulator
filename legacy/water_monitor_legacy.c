/* ============================================================================
 * WATER LEVEL MONITORING SYSTEM (Original Baseline)
 * Source: Environmental Sustainability Project (PDF Reference)
 * ============================================================================
 * Description:
 *   Classifies an entered water level as LOW, MEDIUM, or HIGH.
 *   - 0-29%:   LOW WATER LEVEL (Alert: Check water supply and save water)
 *   - 30-79%:  MEDIUM WATER LEVEL (Message: Water level is in normal range)
 *   - 80-100%: HIGH WATER LEVEL (Alert: Stop filling to avoid overflow)
 * ============================================================================
 */

#include <stdio.h>

int main(void)
{
    int waterLevel;

    printf("====================================\n");
    printf("   WATER LEVEL MONITORING SYSTEM    \n");
    printf("    Environmental Sustainability    \n");
    printf("====================================\n");

    printf("\nEnter water level (0 to 100): ");
    if (scanf("%d", &waterLevel) != 1) {
        printf("Invalid input. Please enter a number.\n");
        return 1;
    }

    if (waterLevel < 0 || waterLevel > 100) {
        printf("Invalid level! Enter a value from 0 to 100.\n");
    }
    else {
        printf("\nWater Level: %d%%\n", waterLevel);

        if (waterLevel < 30) {
            printf("Status: LOW WATER LEVEL\n");
            printf("Alert: Check water supply and save water.\n");
        }
        else if (waterLevel < 80) {
            printf("Status: MEDIUM WATER LEVEL\n");
            printf("Message: Water level is in normal range.\n");
        }
        else {
            printf("Status: HIGH WATER LEVEL\n");
            printf("Alert: Stop filling to avoid overflow.\n");
        }
    }

    printf("\n====================================\n");
    printf("   Save Water, Save Environment!    \n");
    printf("====================================\n");

    return 0;
}
