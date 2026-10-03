from detoxify import Detoxify

textExample = "Stupid bitch im going to hit you"

results = Detoxify('original').predict(textExample)
print(results)
